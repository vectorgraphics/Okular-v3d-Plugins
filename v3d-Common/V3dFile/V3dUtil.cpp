#include "V3dUtil.h"

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

