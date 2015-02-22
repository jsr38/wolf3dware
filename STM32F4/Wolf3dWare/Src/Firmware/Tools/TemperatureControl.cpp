/*
    this file was part of smoothie (http://smoothieware.org/)
    it has been highly modified for this project, and is licensed under the same license as smoothieware
*/

#include "TemperatureControl.h"
#include "../Kernel.h"
#include "../Dispatcher.h"
#include "../GCode.h"

#include <math.h>

// Temp sensor implementations:
#include "TempSensor.h"

TemperatureControl::TemperatureControl(const char *designator, uint8_t index, TempSensor& sensor) : lock(TEMPERATURE_MUTEX), sensor(sensor)
{
	pool_index= index;
	this->designator= designator;
	min_temp_violated= false;
	readonly= false;
	sensor_settings= false; // set to true if sensor settings have been overriden
	use_bangbang = false;
}

TemperatureControl::~TemperatureControl()
{
	if(read_temperature_timer_handle != nullptr)
		xTimerDelete(read_temperature_timer_handle, 10);
}

// void TemperatureControl::on_main_loop(void *argument)
// {
// 	if (this->min_temp_violated) {
// 		THEKERNEL->streams->printf("Error: MINTEMP triggered. Check your temperature sensors!\n");
// 		this->min_temp_violated = false;
// 	}
// }

// required so we can marshall the method for the callback
void temperatureTimerCallback( TimerHandle_t pxTimer )
{
	TemperatureControl *tc = (TemperatureControl*)pvTimerGetTimerID( pxTimer );
	tc->readTemperatureTick();
}

void TemperatureControl::initialize()
{
	const uint32_t readings_per_second= 20;
	PIDdt = 1.0F / readings_per_second;

	// PID P22.0000 I1.0800 D114.0000
	setPIDp(22);
	setPIDi(1.08);
	setPIDd(114);

	// initialize the sensor
	sensor.initialize();

	// register gcode handlers
	// register the gcodes this class handles
	using std::placeholders::_1;

	// M codes
	THEDISPATCHER.addHandler( Dispatcher::MCODE_HANDLER, 105,  std::bind( &TemperatureControl::onGcodeReceived, this, _1) );
	THEDISPATCHER.addHandler( Dispatcher::MCODE_HANDLER, 305,  std::bind( &TemperatureControl::onGcodeReceived, this, _1) );
	THEDISPATCHER.addHandler( Dispatcher::MCODE_HANDLER, 301,  std::bind( &TemperatureControl::onGcodeReceived, this, _1) );
	THEDISPATCHER.addHandler( Dispatcher::MCODE_HANDLER, 500,  std::bind( &TemperatureControl::onGcodeReceived, this, _1) );
	THEDISPATCHER.addHandler( Dispatcher::MCODE_HANDLER, 104,  std::bind( &TemperatureControl::onGcodeReceived, this, _1) );
	THEDISPATCHER.addHandler( Dispatcher::MCODE_HANDLER, 109,  std::bind( &TemperatureControl::onGcodeReceived, this, _1) );

	// setup timer
	if(read_temperature_timer_handle == nullptr) {
		read_temperature_timer_handle= xTimerCreate("TemperatureTimer", pdMS_TO_TICKS(1000/readings_per_second), pdTRUE, (void*)this, temperatureTimerCallback);
		xTimerStart(read_temperature_timer_handle, 10);
	}
}

// TODO  break up into separate methods
bool TemperatureControl::onGcodeReceived(GCode& gc)
{
	AutoLock l(lock);

	if( gc.getCode() == 105) {
		THEDISPATCHER.getOS().printf("%s:%3.1f /%3.1f @%d", designator.c_str(), getTemperature(), target_temperature, pwm_out);
		THEDISPATCHER.getOS().setPrependOK();
		return true;
	}

	if (gc.getCode() == 305) { // set or get sensor settings
		if (gc.hasArg('S') && (gc.getArg('S') == pool_index)) {
			TempSensor::sensor_options_t args= gc.getArgs();
			args.erase('S'); // don't include the S
			if(args.size() > 0) {
				// set the new options
				if(sensor.setOptional(args)) {
					sensor_settings= true;
				}else{
					THEDISPATCHER.getOS().printf("Unable to properly set sensor settings, make sure you specify all required values\n");
				}
			}else{
				// don't override
				sensor_settings= false;
			}

		}else if(!gc.hasArg('S')) {
			THEDISPATCHER.getOS().printf("%s(S%d): using %s\n", designator.c_str(), pool_index, readonly?"Readonly" : use_bangbang?"Bangbang":"PID");
			sensor.getRaw();
			TempSensor::sensor_options_t options;
			if(sensor.getOptional(options)) {
				for(auto &i : options) {
					// foreach optional value
					THEDISPATCHER.getOS().printf("%s(S%d): %c%1.18f\n", designator.c_str(), pool_index, i.first, i.second);
				}
			}
		}

		return true;
	}

	// readonly sensors don't handle the rest
	if(readonly) return false;

	if (gc.getCode() == 301) {
		if (gc.hasArg('S') && (gc.getArg('S') == pool_index)) {
			if (gc.hasArg('P'))
				setPIDp( gc.getArg('P') );
			if (gc.hasArg('I'))
				setPIDi( gc.getArg('I') );
			if (gc.hasArg('D'))
				setPIDd( gc.getArg('D') );
			if (gc.hasArg('X'))
				i_max = gc.getArg('X');
			if (gc.hasArg('Y'))
				max_pwm= gc.getArg('Y');

		}else if(!gc.hasArg('S')) {
			THEDISPATCHER.getOS().printf("%s(S%d): Pf:%g If:%g Df:%g X(I_max):%g max_pwm: %d pwm_out:%d\n", designator.c_str(), pool_index, p_factor, i_factor / PIDdt, d_factor * PIDdt, i_max, max_pwm, pwm_out);
		}
		return true;

	}

	if (gc.getCode() == 500) { // M500 saves some volatile settings to non volatile storage
		THEDISPATCHER.getOS().printf("M301 S%d P%1.4f I%1.4f D%1.4f X%1.4f Y%d\n", pool_index, p_factor, i_factor / PIDdt, d_factor * PIDdt, i_max, max_pwm);

		if(sensor_settings) {
			// get or save any sensor specific optional values
			TempSensor::sensor_options_t options;
			if(sensor.getOptional(options) && !options.empty()) {
				THEDISPATCHER.getOS().printf(";Optional temp sensor specific settings:\nM305 S%d", pool_index);
				for(auto &i : options) {
					THEDISPATCHER.getOS().printf(" %c%1.18f", i.first, i.second);
				}
				THEDISPATCHER.getOS().printf("\n");
			}
		}
		return true;

	}

	if( ( gc.getCode() == 104 || gc.getCode() == 109 ) && gc.hasArg('S')) {
		// this only gets handled if it is not controlled by the tool manager or is active in the toolmanager
		active = true;

		if(active) {
			// TODO required so temp change happens in order need to wait on a barrier/rendevous
			//THEKERNEL->conveyor->wait_for_empty_queue();

			float v = gc.getArg('S');
			setDesiredTemperature(v);
			// wait for temp to be reached, no more gcodes will be fetched until this is complete
			// TODO is there a better way to stall processing in these cases?
			if( gc.getCode() == 109) {
				while ( getTemperature() < target_temperature ) {
					// this gets sent immediately
					//THEKERNEL.OOBPrintf("%s:%3.1f /%3.1f @%d\n", designator.c_str(), get_temperature(), ((target_temperature == 0) ? 0.0F : target_temperature), pwm_out);
					// wait 1 second
					const TickType_t xDelay = pdMS_TO_TICKS( 1000 );
					vTaskDelay( xDelay );
				}
			}
		}
		return true;
	}

	return false;
}

void TemperatureControl::setDesiredTemperature(float desired_temperature)
{
	// Never go over the configured max temperature
	if( desired_temperature > max_temp ){
		desired_temperature = max_temp;
	}

	float last_target_temperature= target_temperature;
	target_temperature = desired_temperature;
	if (desired_temperature == 0.0F){
		// turning it off
		pwm_out = 0;
		setPWM(pool_index, 0);

	}else if(last_target_temperature == 0.0F) {
		// if it was off and we are now turning it on we need to initialize
		last_input= last_reading;
		// set to whatever the output currently is See http://brettbeauregard.com/blog/2011/04/improving-the-beginner%E2%80%99s-pid-initialization/
		i_term= pwm_out;
		if (i_term > i_max) i_term = i_max;
		else if (i_term < 0.0F) i_term = 0.0F;
	}
}

// called from Timer tick Thread
void TemperatureControl::readTemperatureTick()
{
	AutoLock l(lock);

	float temperature = sensor.getTemperature();
	last_reading = temperature;

	if(std::isinf(temperature)) {
		// temperature read error
		min_temp_violated = true;
		target_temperature = 0;
		pwm_out= 0;
		if(!readonly) {
			// turn heater off
			setPWM(pool_index, 0);
		}

	}else if(target_temperature > 0) {
		// do the PID process on current temperature
		pidProcess(temperature);
	}
}

/**
 * Based on https://github.com/br3ttb/Arduino-PID-Library
 */
void TemperatureControl::pidProcess(float temperature)
{
	if(use_bangbang) {
		// bang bang is very simple, if temp is < target - hysteresis turn on full else if  temp is > target + hysteresis turn heater off
		// good for relays
		if(temperature > (target_temperature + hysteresis) && pwm_out > 0) {
			pwm_out= 0;
			setPWM(pool_index, 0);

		} else if(temperature < (target_temperature - hysteresis) && pwm_out == 0) {
			if(max_pwm < 100) {
				// only to whatever max pwm is configured
				setPWM(pool_index, max_pwm);
				pwm_out= max_pwm; // for display purposes only

			} else {
				// turn on full
				setPWM(pool_index, 100);
				pwm_out= 100;
			}
		}
		return;
	}

	// regular PID control
	float error = target_temperature - temperature;
	float new_i = i_term + (error * i_factor);

	if (new_i > i_max) new_i = i_max;
	else if (new_i < 0.0F) new_i = 0.0F;

 	i_term= new_i;

	float d = (temperature - last_input);

	// calculate the PID output
	float o = (p_factor * error) + new_i - (d_factor * d);

	if (o >= max_pwm){
		o = max_pwm;

	}else if(o < 0) {
		o = 0;

	}

	pwm_out= roundf(o);
	setPWM(pool_index, pwm_out);
	last_input = temperature;
}
