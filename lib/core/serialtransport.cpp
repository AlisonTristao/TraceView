#include "serialtransport.h"

namespace traceview {

QByteArray lineTerminatorBytes(LineTerminator terminator) {
    switch (terminator) {
        case LineTerminator::None:
            return QByteArray();
        case LineTerminator::Lf:
            return QByteArray("\n");
        case LineTerminator::Cr:
            return QByteArray("\r");
        case LineTerminator::CrLf:
            return QByteArray("\r\n");
    }
    return QByteArray();
}

}  // namespace traceview
