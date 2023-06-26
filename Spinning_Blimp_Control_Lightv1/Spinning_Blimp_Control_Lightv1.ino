#include <crazyflieComplementary.h>
#include "WiFi.h"
#include "AsyncUDP.h"
#include <ESP32Servo.h>

#define THRUST1 D0
#define THRUST2 D1
#define LightSensor1 A2
#define LightSensor2 A3

// min and max high signal of thruster PWMs
int minUs = 1000;
int maxUs = 2000;

// Wi-Fi access details
const char * ssid = "AIRLab-BigLab";
const char * password = "Airlabrocks2022";

// using servo lib to control brushless motors
Servo thrust1;
Servo thrust2; 

// c.c. Edward
SensFusion sensorSuite;

AsyncUDP udp;
float joy_data[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
volatile bool joy_ready = false;
volatile unsigned long time_now, time_loop; 

// P.I.D. Values
float roll, pitch, yaw;
float rollrate, pitchrate, yawrate;
float estimatedZ, velocityZ, groundZ;
float abz = 0.0;
float kpz = 0.01*20.0; // N/meter
float kdz = 0.2;
float kpx = 0.4;
float kdx = 0.2;
float kptz = 0.3;
float kdtz = -0.025;
float kptx = 0.01;
float kdtx = 0.01;
float lx = 0.25;
float m1 = 0.0;
float m2 = 0.0;
// lightSensor 
bool enableLight = true;
float kpLight = 100.0;
float light1 = 0;
float light2 = 0;

void setup() {
  Serial.begin(9600); 
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed");
    while (1) {
      delay(1000);
    }
  }

  // Allocate timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  // Standard 50hz PWM for thrusters
  thrust1.setPeriodHertz(50);
  thrust2.setPeriodHertz(50);

  thrust1.attach(THRUST1, minUs, maxUs);
  thrust2.attach(THRUST2, minUs, maxUs);
  delay(100);

  // ESC arm
  escarm();
  
  // Access sensor suite c.c. Edward
  sensorSuite.initSensors();
  sensorSuite.updateKp(5, -1, 0.3); // 20, -1, 0
  groundZ = sensorSuite.returnZ();
  // sensorSuite.recordData();

  // magnetometer calibration
  float transformationMatrix[3][3] = {
    {1.0f, 9.693f, 0.6187f},
    {9.6624f, -0.6822f, 0.3864f},
    {-0.4155f, 0.6628f, -10.7386f}
  };
  float offsets[3] = {11.98f, 7.01f, 21.77f};
  sensorSuite.enterTransform(offsets, transformationMatrix);
  getSensorValues();

  
  time_now = millis();
  // time_loop = millis();
  if(udp.listen(1234)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());

    // setup callback functions of the udp
    udp.onPacket([](AsyncUDPPacket packet) {
      joy_ready = false;
      time_now = millis();
      unsigned char *buffer = packet.data();
      unpack_joystick(joy_data, buffer);
      joy_ready = true;
      //reply to the server
      // packet.printf("Got %u bytes of data", packet.length());
    });
  }
    //gyro, acc, mag, euler, z

}

void loop() {
    //gyro, acc, mag, euler, z
  float cfx, cfy, cfz, ctx, cty, ctz;
  
  // read lightsensor value (normalized to 0-1), smaller value means brighter
  light1 = analogRead(LightSensor1)/5000;
  light2 = analogRead(LightSensor2)/5000;
  // Runs the sensor fusion loop
  sensorSuite.sensfusionLoop(false, 5);

  if (joy_data[7] != 1){

    // Serial.println("Initialization") //debug;
    thrust1.writeMicroseconds(minUs);
    thrust2.writeMicroseconds(minUs);

    // Debug joystick input
    // thrust1.writeMicroseconds((int) (1000 + joy_data[0]));
    // thrust2.writeMicroseconds((int) (1000 + joy_data[0]));

  } else if (joy_ready && millis() - time_now <= 1000){ //&& millis() - time_loop > 50) {kdz
    // Call sensor suite to update 10-DOF values
    getSensorValues();
    getControllerInputs(&cfx, &cfy, &cfz, &ctx, &cty, &ctz, &abz);
    addFeedback(&cfx, &cfy, &cfz, &ctx, &cty, &ctz, abz);
    controlOutputs(cfx, cfy, cfz, ctx, cty, ctz);

    int m1us,m2us;
    // Convert motor input to 1000-2000 Us values
    if (enableLight == 0){
      m1us = (minUs + (maxUs - minUs)*m1*3.33);
      m2us = (minUs + (maxUs - minUs)*m2*3.33);
    }
    else{
    m1us = (minUs + (maxUs - minUs)*m1*3.33) + kpLight*(light1 - light2);
    m2us = (minUs + (maxUs - minUs)*m2*3.33) - kpLight*(light1 - light2);
    }
    // Write motor thrust values to the pins
    thrust1.write((int) m1us);
    thrust2.write((int) m2us);

    // Else statement for if the Wi-Fi signal is lost
  } else {
    thrust1.writeMicroseconds((int) minUs);
    thrust2.writeMicroseconds((int) minUs);
    
  }

}

//Enter arming sequence for ESC
void escarm(){
  // ESC arming sequence for BLHeli S
  thrust1.writeMicroseconds(1000);
  delay(10);
  thrust2.writeMicroseconds(1000);
  delay(10);

  // Sweep up
  for(int i=1100; i<1500; i++) {
    thrust1.writeMicroseconds(i);
    delay(5);
    thrust2.writeMicroseconds(i);
    delay(5);
  }
  // Sweep down
  for(int i=1500; i<1100; i--) {
    thrust1.writeMicroseconds(i);
    delay(5);
    thrust2.writeMicroseconds(i);
    delay(5);
  }
  // Back to minimum value
  thrust1.writeMicroseconds(1000);
  delay(10);
  thrust2.writeMicroseconds(1000);
  delay(10);

}


void getSensorValues(){ 
  //all in radians or meters or meters per second
  // roll = sensorSuite.getRoll();
  // pitch = -1*sensorSuite.getPitch();
  yaw = sensorSuite.getYaw();
  // rollrate = sensorSuite.getRollRate();
  // pitchrate = sensorSuite.getPitchRate();
  yawrate = sensorSuite.getYawRate();
  estimatedZ = sensorSuite.returnZ();
  velocityZ = sensorSuite.returnVZ(); 
}

float valtz = 0;
void getControllerInputs(float *fx, float *fy, float *fz, float *tx, float *ty, float *tz, float *abz){
  if (false) {
    *fx = 0;//joy_data[0];
    *fy = 0;//joy_data[1];
    *fz = 0;//joy_data[2];
    *tx = 0;//joy_data[3];
    *ty = 0;//joy_data[4];
    *tz = 0;//joy_data[5];
    *abz = 0;//joy_data[6];
    if (valtz > 1){
      valtz = -1;
    } else {
      valtz += .01;
    }
  } else{
  *fx = joy_data[0];
  *fy = joy_data[1];
  *fz = joy_data[2];
  *tx = joy_data[3];
  *ty = joy_data[4];
  *tz = joy_data[5];
  *abz = joy_data[6];
  enableLight = joy_data[3];
  }
}
void addFeedback(float *fx, float *fy, float *fz, float *tx, float *ty, float *tz, float abz){
    *fz = (*fz  - (estimatedZ-groundZ))*kpz - (velocityZ)*kdz + abz;//*fz = *fz + abz;//

}
float clamp(float in, float min, float max){
  if (in< min){
    return min;
  } else if (in > max){
    return max;
  } else {
    return in;
  }
}

void controlOutputs(float ifx, float ify, float ifz, float itx, float ity, float itz) {
  //float desiredPitch = wty - self->pitch*(float)g_self.kR_xy - self->pitchrate *(float)g_self.kw_xy;
  // // sinr = (float) sin(self->roll);


  float term1 = kpz*radians(ifx)*cos(radians(yaw));
  float term2 = kpz*radians(ify)*cos(radians(yaw));
  // float term1 = 2.0*radians(ifx)*cos(radians(itz));
  // float term2 = 2.0*radians(ify)*cos(radians(itz));
  
  Serial.println(yaw);
  // Convert joystick input to theta and magnitude for cyclic input
  float joytheta = tan(ifx/ify);
  float joymag   = pow(radians(ifx),2) + pow(radians(ify),2);
  float coscos   = cos(joytheta)*cos(radians(yaw));
  float sincos   = sin(joytheta)*cos(radians(yaw));

  // Cyclic pitch input
  float lhs = joymag*(coscos  - sincos);
  float rhs = joymag*(-sincos - coscos);

  // TODO: bring back x-y feedback
  // float f1 = ifz; // + ifx + ify; // LHS motor
  // float f2 = ifz; // - ifx - ify; // RHS motor
  float f1 = ifz + lhs + rhs; // LHS motor
  float f2 = ifz - lhs - rhs; // RHS motor
  m1 = clamp(f1, 0, 0.25);
  m2 = clamp(f2, 0, 0.25);

}

// Having questions? Ask Jiawei!
void unpack_joystick(float *dat, const unsigned char *buffer) {
  int num_floats = 8;
  int num_bytes = 4;
  int i, j;

  for(i = 0; i < num_floats; i++) {
    char temp[4] = {0, 0, 0, 0};
    for(j = 0; j < num_bytes; j++) {
      temp[j] = buffer[num_bytes*i + j];
    }
    dat[i] = *((float*) temp);

  }
;
}
