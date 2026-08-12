#include "V3dHeaderInfo.h"

#include <iostream>

void V3dHeaderInfo::print() {
    std::cout << "canvasWidth: " << canvasWidth << std::endl;
    std::cout << "canvasHeight: " << canvasHeight << std::endl;
    std::cout << "absolute: " <<  absolute << std::endl;
    std::cout << "minBound: (" << minBound.x << ", " << minBound.y << ", " << minBound.z << ")" << std::endl;
    std::cout << "maxBound: (" << maxBound.x << ", " << maxBound.y << ", " << maxBound.z << ")" << std::endl;
    std::cout << "orthographic: " << orthographic << std::endl;
    std::cout << "angleOfView: " << angleOfView << std::endl;
    std::cout << "initialZoom: " << initialZoom << std::endl;
    std::cout << "viewportShift: (" << viewportShift.x << ", " << viewportShift.y << ")" << std::endl;
    std::cout << "viewportMargin: (" << viewportMargin.x << ", " << viewportMargin.y << ")" << std::endl;
    std::cout << "Lights (" << lights.size() << "):" << std::endl;
    for (size_t i = 0; i < lights.size(); ++i) {
        std::cout << "  [" << i << "] direction: (" << lights[i].direction.x << ", " << lights[i].direction.y << ", " << lights[i].direction.z << ")" << std::endl;
        std::cout << "       color: (" << lights[i].color.x << ", " << lights[i].color.y << ", " << lights[i].color.z << ")" << std::endl;
    }

    std::cout << "background: (" << background.x << ", " << background.y << ", " << background.z << ", " << background.w << ")" << std::endl;
    std::cout << "zoomFactor: " << zoomFactor << std::endl;
    std::cout << "zoomPinchFactor: " << zoomPinchFactor << std::endl;
    std::cout << "zoomPinchCap: " << zoomPinchCap << std::endl;
    std::cout << "zoomStep: " << zoomStep << std::endl;
    std::cout << "shiftHoldDistance: " << shiftHoldDistance << std::endl;
    std::cout << "shiftWaitTime: " << shiftWaitTime << std::endl;
    std::cout << "vibrateTime: " << vibrateTime << std::endl;
    std::cout << "imageName: " << imageName << std::endl;
}
