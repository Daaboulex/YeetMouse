#ifndef RAWACCELORACLE_H
#define RAWACCELORACLE_H

#include <memory>

#include "gui/RawAccel.h"

class RawAccelOracle {
public:
    struct Output {
        double x;
        double y;
        long countsX;
        long countsY;
    };

    RawAccelOracle(const RawAccel::Profile &profile, const RawAccel::DeviceConfig &device);
    ~RawAccelOracle();

    Output Packet(int dx, int dy, double measuredMs);

private:
    struct State;
    std::unique_ptr<State> state;
};

#endif //RAWACCELORACLE_H
