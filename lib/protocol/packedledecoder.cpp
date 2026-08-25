#include "protocol/packedledecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace traceview {

namespace {

// Assembles `width` little-endian bytes starting at `offset` into an
// unsigned integer -- manual byte-by-byte shifting (not a pointer cast)
// keeps this correct regardless of host endianness, same approach the old
// chartdata.cpp hex decoder used for the legacy protocol.
quint64 readRawLe(const QByteArray& body, int offset, int width) {
    quint64 raw = 0;
    for (int i = width - 1; i >= 0; --i) {
        raw = (raw << 8) | quint8(body[offset + i]);
    }
    return raw;
}

// Sign-extends the low `width` bytes of `raw` to a signed 64-bit value.
qint64 signExtend(quint64 raw, int width) {
    const int bits = width * 8;
    if (bits >= 64) {
        return qint64(raw);
    }
    const quint64 signBit = quint64(1) << (bits - 1);
    if (raw & signBit) {
        return qint64(raw | (~quint64(0) << bits));
    }
    return qint64(raw);
}

bool isIntegerType(TelemetryFieldType type) {
    switch (type) {
        case TelemetryFieldType::UInt8:
        case TelemetryFieldType::UInt16:
        case TelemetryFieldType::UInt32:
        case TelemetryFieldType::UInt64:
        case TelemetryFieldType::Int8:
        case TelemetryFieldType::Int16:
        case TelemetryFieldType::Int32:
        case TelemetryFieldType::Int64:
            return true;
        default:
            return false;
    }
}

bool isSignedType(TelemetryFieldType type) {
    switch (type) {
        case TelemetryFieldType::Int8:
        case TelemetryFieldType::Int16:
        case TelemetryFieldType::Int32:
        case TelemetryFieldType::Int64:
            return true;
        default:
            return false;
    }
}

// Decodes one field's `count` consecutive elements starting at `offset` into
// `outElements`, applying scale/offset per telemetry.md section 3 (skipped
// for bool/enum, section 6's non-finite check for floats). Returns false
// on truncation or a non-finite float; `offset` is only advanced when this
// returns true.
bool decodeElements(const TelemetryFieldSchema& field, const QByteArray& body, int* offset,
                    int count, QVector<double>* outElements) {
    const int width = telemetryFieldTypeWidth(field.type);
    if (width <= 0) {
        return false;
    }
    if (*offset < 0 || count < 0 || body.size() - *offset < qint64(width) * qint64(count)) {
        return false;
    }

    outElements->reserve(outElements->size() + count);
    for (int i = 0; i < count; ++i) {
        const int elementOffset = *offset + i * width;
        const quint64 raw = readRawLe(body, elementOffset, width);

        double value = 0.0;
        switch (field.type) {
            case TelemetryFieldType::Bool:
                if (raw != 0 && raw != 1) {
                    return false;  // telemetry.md section 5: only 0x00/0x01 valid
                }
                value = double(raw);
                break;
            case TelemetryFieldType::Enum8:
            case TelemetryFieldType::Enum16:
                // Raw integer always, regardless of any declared scale/offset
                // (telemetry.md section 3/6: label selection -- and by
                // extension the value this decoder exposes -- always uses
                // the raw integer).
                value = double(raw);
                break;
            case TelemetryFieldType::Float32: {
                const quint32 bits32 = quint32(raw);
                float f = 0.0f;
                std::memcpy(&f, &bits32, sizeof(f));
                if (!std::isfinite(double(f))) {
                    return false;  // telemetry.md section 6
                }
                value = double(f) * field.scale + field.offset;
                break;
            }
            case TelemetryFieldType::Float64: {
                double d = 0.0;
                std::memcpy(&d, &raw, sizeof(d));
                if (!std::isfinite(d)) {
                    return false;  // telemetry.md section 6
                }
                value = d * field.scale + field.offset;
                break;
            }
            default:
                if (isIntegerType(field.type)) {
                    const double rawValue =
                        isSignedType(field.type) ? double(signExtend(raw, width)) : double(raw);
                    value = rawValue * field.scale + field.offset;
                } else {
                    return false;  // unreachable for a well-formed schema
                }
                break;
        }
        outElements->append(value);
    }
    *offset += width * count;
    return true;
}

}  // namespace

bool decodePackedLe(const TelemetryTopicSchema& schema, const QByteArray& body,
                    QHash<quint16, TelemetryFieldValue>* outValues) {
    if (outValues == nullptr) {
        return false;
    }

    QVector<const TelemetryFieldSchema*> orderedFields;
    orderedFields.reserve(schema.fields.size());
    for (const TelemetryFieldSchema& field : schema.fields) {
        orderedFields.append(&field);
    }
    std::stable_sort(orderedFields.begin(), orderedFields.end(),
                     [](const TelemetryFieldSchema* a, const TelemetryFieldSchema* b) {
                         return a->order < b->order;
                     });

    int nullableCount = 0;
    for (const TelemetryFieldSchema* field : orderedFields) {
        if (field->nullable) {
            ++nullableCount;
        }
    }
    const int bitmapBytes = (nullableCount + 7) / 8;
    if (body.size() < bitmapBytes) {
        return false;  // telemetry.md section 6: too short to even hold the bitmap
    }

    // Bits not assigned to a nullable field in the last bitmap octet MUST be
    // zero (telemetry.md section 4.1).
    if (nullableCount > 0 && nullableCount % 8 != 0) {
        const quint8 lastByte = quint8(body[bitmapBytes - 1]);
        const int usedBits = nullableCount % 8;
        const quint8 unusedMask = quint8(0xFFu << usedBits);
        if ((lastByte & unusedMask) != 0) {
            return false;
        }
    }

    auto bitSet = [&](int bitIndex) -> bool {
        const int byteIndex = bitIndex / 8;
        const int bitInByte = bitIndex % 8;
        return (quint8(body[byteIndex]) & (1u << bitInByte)) != 0;
    };

    QHash<quint16, TelemetryFieldValue> values;
    int offset = bitmapBytes;
    int nullableIndex = 0;

    for (const TelemetryFieldSchema* fieldPtr : orderedFields) {
        const TelemetryFieldSchema& field = *fieldPtr;

        if (field.nullable) {
            const bool present = bitSet(nullableIndex);
            ++nullableIndex;
            if (!present) {
                TelemetryFieldValue value;
                value.isNull = true;
                values.insert(field.fieldId, value);
                continue;
            }
        }

        int count = field.elementCount;
        if (field.isVariableLength()) {
            if (body.size() - offset < 2) {
                return false;
            }
            count = int(quint16(quint8(body[offset])) | (quint16(quint8(body[offset + 1])) << 8));
            offset += 2;
            if (count > field.maxElementCount) {
                return false;  // telemetry.md section 4.1
            }
        } else if (count < 1) {
            return false;  // malformed schema: fixed field with no width
        }

        QVector<double> elements;
        if (!decodeElements(field, body, &offset, count, &elements)) {
            return false;
        }

        TelemetryFieldValue value;
        value.isNull = false;
        value.elements = elements;
        values.insert(field.fieldId, value);
    }

    if (offset != body.size()) {
        return false;  // telemetry.md section 6: bytes left over
    }

    *outValues = std::move(values);
    return true;
}

}  // namespace traceview
