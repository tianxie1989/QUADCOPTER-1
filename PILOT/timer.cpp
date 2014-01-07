/*
 * File:   Timer.cpp
 * Author: matt
 *
 * Created on 05 November 2012, 10:19
 */

#include "timer.h"

#define PERIOD 2500000

TimerClass Timer;
pthread_mutex_t TimerMutex_;

const int YAW=0;
const int PITCH=1;
const int ROLL=2;
const int DIM=3;


TimerClass::TimerClass()
{
  /* Intialize sigaction struct */
   signalAction.sa_handler = &sig_handler_;

   /* Connect a signal handler routine to the SIGALRM signal */
   sigaction(SIGALRM, &signalAction, NULL);

   /* Allocate a timer */
   timer_create(CLOCK_REALTIME, NULL, &timerId);

   started = false;
 }

 TimerClass::TimerClass(const TimerClass& orig)
 {
 }

 TimerClass::~TimerClass()
 {
 }

 void TimerClass::start()
 {
   timeValue_.tv_sec = 0;
   timeValue_.tv_nsec = PERIOD;
   timeToSet_.it_value = timeValue_;
   timer_settime(timerId, 0, &timeToSet_, NULL);
   started = true;
 }

 void TimerClass::stop()
 {
   timeValue_.tv_sec = 0;
   timeValue_.tv_nsec = 0;
   timeToSet_.it_value = timeValue_;
   timer_settime(timerId, 0, &timeToSet_, NULL);
   started = false;
 }

 inline void TimerClass::calcdt_()
 {
   oldtime_ = time_;
   clock_gettime(CLOCK_MONOTONIC, &time_);
   Timer.dt = ((static_cast <int64_t>(time_.tv_sec) * 1000000000 +
		static_cast <int64_t>(time_.tv_nsec)) -
	       (static_cast <int64_t>(oldtime_.tv_sec) * 1000000000 +
		static_cast <int64_t>(oldtime_.tv_nsec))) / 1000000000.0;
}

inline void TimerClass::compensate_()
{
  //Timer aims to get as close to 400Hz as possible, mostly limited by the I2C
  //bandwidth
  clock_gettime(CLOCK_MONOTONIC, &iterationtime_);
  long inttime = PERIOD - ((iterationtime_.tv_sec * 1000000000 +
			    iterationtime_.tv_nsec) - (time_.tv_sec * 1000000000
						       + time_.tv_nsec));
  if (inttime < 0)
    Timer.timeValue_.tv_nsec = 1;
  else
    Timer.timeValue_.tv_nsec = inttime;
  Timer.timeToSet_.it_value = Timer.timeValue_;
  timer_settime(timerId, 0, &timeToSet_, NULL);
}

template <class T, void(T::*member_function)()>
void* thunk(void* p)
{
  (static_cast<T*> (p)->*member_function)();
  return 0;
}

void TimerClass::sig_handler_(int signum)
{
  pthread_mutex_lock(&TimerMutex_);

  //1-Get target values from remote
  float thr=140;
  wifi.get_target(thr,
		  yaw.setpoint,
		  pitch.setpoint,
		  roll.setpoint);

  // get attitude of the drone
  imu.getAttitude();

  //Timer dt
  Timer.calcdt_();

  // PID on attitude
  yaw.update_pid   (imu.ypr[YAW]);
  pitch.update_pid (imu.ypr[PITCH]);
  roll.update_pid  (imu.ypr[ROLL]);

  yawRate.setpoint   = yaw.output;
  pitchRate.setpoint = pitch.output;
  rollRate.setpoint  = roll.output;

  yawRate.update_pid   (imu.gyro[YAW]);
  pitchRate.update_pid (imu.gyro[PITCH]);
  rollRate.update_pid  (imu.gyro[ROLL]);

  //ESC update
  ESC.servoval[0] =(int)(thr - rollRate.output);//  + pid_out[YAW]);
  ESC.servoval[1] =(int)(thr + rollRate.output);//  + pid_out[YAW]);
  ESC.servoval[2] =(int)(thr - pitchRate.output);// - pid_out[YAW]);
  ESC.servoval[3] =(int)(thr + pitchRate.output);// - pid_out[YAW]);
  ESC.setServo();

  //timer end
  Timer.compensate_();
  pthread_mutex_unlock(&TimerMutex_);
}
