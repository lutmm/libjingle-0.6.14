/*
 * libjingle
 * Copyright 2004--2011, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TALK_SESSION_PHONE_FAKENETWORKINTERFACE_H_
#define TALK_SESSION_PHONE_FAKENETWORKINTERFACE_H_

#include <vector>

#include "talk/base/buffer.h"
#include "talk/base/byteorder.h"
#include "talk/base/criticalsection.h"
#include "talk/base/messagehandler.h"
#include "talk/base/messagequeue.h"
#include "talk/base/thread.h"
#include "talk/session/phone/mediachannel.h"

namespace cricket {

// Fake NetworkInterface that sends/receives RTP/RTCP packets.
class FakeNetworkInterface : public MediaChannel::NetworkInterface,
                             public talk_base::MessageHandler {
 public:
  FakeNetworkInterface()
      : thread_(talk_base::Thread::Current()),
        dest_(NULL),
        conf_(false),
        sendbuf_size_(-1),
        recvbuf_size_(-1) {
  }

  void SetDestination(MediaChannel* dest) { dest_ = dest; }

  // Conference mode is a mode where instead of simply forwarding the packets,
  // the transport will send multiple copies of the packet with the specified
  // SSRCs. This allows us to simulate receiving media from multiple sources.
  void SetConferenceMode(bool conf, const std::vector<uint32>& ssrcs) {
    conf_ = conf;
    ssrcs_ = ssrcs;
  }

  int NumRtpBytes() {
    talk_base::CritScope cs(&crit_);
    int bytes = 0;
    for (size_t i = 0; i < rtp_packets_.size(); ++i) {
      bytes += rtp_packets_[i].length();
    }
    return bytes;
  }

  int NumRtpPackets() {
    talk_base::CritScope cs(&crit_);
    return rtp_packets_.size();
  }

  // Note: callers are responsible for deleting the returned buffer.
  const talk_base::Buffer* GetRtpPacket(int index) {
    talk_base::CritScope cs(&crit_);
    if (index >= NumRtpPackets()) {
      return NULL;
    }
    return new talk_base::Buffer(rtp_packets_[index]);
  }

  int NumRtcpPackets() {
    talk_base::CritScope cs(&crit_);
    return rtcp_packets_.size();
  }

  // Note: callers are responsible for deleting the returned buffer.
  const talk_base::Buffer* GetRtcpPacket(int index) {
    talk_base::CritScope cs(&crit_);
    if (index >= NumRtcpPackets()) {
      return NULL;
    }
    return new talk_base::Buffer(rtcp_packets_[index]);
  }

  int sendbuf_size() const { return sendbuf_size_; }
  int recvbuf_size() const { return recvbuf_size_; }

 protected:
  virtual bool SendPacket(talk_base::Buffer* packet) {
    talk_base::CritScope cs(&crit_);
    rtp_packets_.push_back(*packet);
    if (conf_) {
      talk_base::Buffer buffer_copy(*packet);
      for (size_t i = 0; i < ssrcs_.size(); ++i) {
        talk_base::SetBE32(buffer_copy.data() + 8, ssrcs_[i]);
        PostMessage(ST_RTP, buffer_copy);
      }
    } else {
      PostMessage(ST_RTP, *packet);
    }
    return true;
  }

  virtual bool SendRtcp(talk_base::Buffer* packet) {
    talk_base::CritScope cs(&crit_);
    rtcp_packets_.push_back(*packet);
    if (!conf_) {
      // don't worry about RTCP in conf mode for now
      PostMessage(ST_RTCP, *packet);
    }
    return true;
  }

  virtual int SetOption(SocketType type, talk_base::Socket::Option opt,
                        int option) {
    if (opt == talk_base::Socket::OPT_SNDBUF) {
      sendbuf_size_ = option;
    } else if (opt == talk_base::Socket::OPT_RCVBUF) {
      recvbuf_size_ = option;
    }
    return 0;
  }

  void PostMessage(int id, const talk_base::Buffer& packet) {
    thread_->Post(this, id, talk_base::WrapMessageData(packet));
  }

  virtual void OnMessage(talk_base::Message* msg) {
    talk_base::TypedMessageData<talk_base::Buffer>* msg_data =
        static_cast<talk_base::TypedMessageData<talk_base::Buffer>*>(
            msg->pdata);
    if (dest_) {
      if (msg->message_id == ST_RTP) {
        dest_->OnPacketReceived(&msg_data->data());
      } else {
        dest_->OnRtcpReceived(&msg_data->data());
      }
    }
    delete msg_data;
  }

 private:
  talk_base::Thread* thread_;
  MediaChannel* dest_;
  bool conf_;
  std::vector<uint32> ssrcs_;
  talk_base::CriticalSection crit_;
  std::vector<talk_base::Buffer> rtp_packets_;
  std::vector<talk_base::Buffer> rtcp_packets_;
  int sendbuf_size_;
  int recvbuf_size_;
};

}  // namespace cricket

#endif  // TALK_SESSION_PHONE_FAKENETWORKINTERFACE_H_
