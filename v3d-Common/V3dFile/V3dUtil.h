#pragma once

#include "xstream.h"
#include "V3dTypes.h"

double readReal(xdr::ixstream& xdrFile, V3D_BOOL doublePrecision);

// Bytes left in the stream from the current position (0 if the position is
// unknown). totalBytes is the stream's full size, known up front: the file
// size for file streams, the buffer size for in-memory streams.
int64_t bytesRemaining(xdr::ixstream& xdrFile, int64_t totalBytes);
