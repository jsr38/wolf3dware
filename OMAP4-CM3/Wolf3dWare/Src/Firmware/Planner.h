#pragma once

#include "Block.h"

#include <stdint.h>
#include <deque>

class GCode;
class MotionControl;
class Actuator;

class Planner
{
public:
	Planner();
	~Planner(){};
	void reset();
	void initialize();
	bool plan(const float *last_target, const float *target, int n_axis, Actuator *actuators, float rate_mms);
	void dump(std::ostream& o) const;
	void purge();

	using Queue_t = std::deque<Block>;
	Queue_t& getLookAheadQueue() { return lookahead_q; }
	Queue_t& getReadyQueue() { return ready_q; }
	void moveAllToReady();

private:
	void calculateTrapezoid(Block& block, float entryspeed, float exitspeed);
  	float maxAllowableSpeed( float acceleration, float target_velocity, float distance) const;
	float maxExitSpeed(const Block& b) const;
	float reversePass(Block &b, float exit_speed);
	float forwardPass(Block &b, float prev_max_exit_speed);
    void recalculate();
	bool isSoloMove(const Block& block, char axis);

	bool handleConfigurations(GCode&);
	bool handleSaveConfiguration(GCode &gc);

	Queue_t lookahead_q;
	Queue_t ready_q;

    float previous_unit_vec[3];
	float previous_nominal_speed{0};

    float default_acceleration{2000};
    float junction_deviation{0.05F};
    float z_junction_deviation{-1};
    float minimum_planner_speed{0};
};
