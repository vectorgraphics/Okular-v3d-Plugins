#include "V3dUtil.h"

int64_t bytesRemaining(xdr::ixstream& xdrFile, int64_t totalBytes) {
    int64_t pos = xdrFile.tell();
    return (pos >= 0 && totalBytes >= pos) ? (totalBytes - pos) : 0;
}

double readReal(xdr::ixstream& xdrFile, V3D_BOOL doublePrecision) {
    if (doublePrecision) {
        double val;
        xdrFile >> val;
        return val;
    } else {
        float val;
        xdrFile >> val;
        return static_cast<double>(val);
    }
}

