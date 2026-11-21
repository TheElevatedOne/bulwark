#include "transport.h"

#include "protocol.h"

namespace bw {

bool isQueryCommand(const gip::Frame& f) {
    if (f.id == gip::kAcknowledge) return true;  // acknowledging the device changes nothing
    if (f.id != gip::kVendor || f.payload.empty()) return false;
    switch (f.payload[0]) {
    case cmd::kReadRange:
    case cmd::kVersion:
    case cmd::kReadProfileIndex:
    case cmd::kStreamStart:  // starts/keeps the E0 data stream; changes no setting
        return true;
    default:
        return false;
    }
}

void ReadOnlyTransport::send(const gip::Frame& frame) {
    if (!isQueryCommand(frame))
        throw WriteRefused("read-only mode: refused " + gip::describe(frame));
    inner_->send(frame);
}

}  // namespace bw
