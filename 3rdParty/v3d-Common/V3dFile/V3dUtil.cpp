#include "V3dUtil.h"

float readReal(xdr::ixstream& xdrFile, BOOL doublePrecision) {
    float out;
    if (doublePrecision) {
        double val;
        xdrFile >> val;
        out = static_cast<float>(val);
    } else {
        xdrFile >> out;
    }

    return out;
}