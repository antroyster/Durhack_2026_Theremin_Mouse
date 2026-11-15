// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include </home/anthony-royster/Durhack_2026_Theremin_Mouse/Non-claude-new/Eigen/Eigen>
#include <iostream>
#include <thread>
#include <chrono>
#include <glob.h>
#include </home/anthony-royster/Durhack_2026_Theremin_Mouse/src/common/crc16.c>
#include <string.h>
#define _Static_assert static_assert   // protocol.h is C; this is C++'s spelling of the same check
extern "C" {                           // imu_reader is C code: tell C++ not to rename its functions
#include </home/anthony-royster/Durhack_2026_Theremin_Mouse/Non-claude-new/imu_reader.h>
}


using namespace Eigen;
using namespace std;
using namespace this_thread;
using namespace chrono;
static std::string find_port() {
    glob_t g; std::string r;
    if (glob("/dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00", 0, nullptr, &g) == 0 && g.gl_pathc > 0) r = g.gl_pathv[0];
    globfree(&g);
    return r;
}
class package {
public:
    Vector3f acceleration;
    Vector3f velocity;
    Vector3f position;
    Matrix3f Rtotal;
    float roll;
    float pitch;
    float yaw;


    package(Vector3f a,Vector3f v, Vector3f s, Matrix3f angle, float ry, float rx, float rz) {
        acceleration = a;
        velocity = v;
        position = s;
        Rtotal = angle;
        roll = ry;
        pitch = rx;
        yaw = rz;
    };

};
package getaccelerations(Vector3f Acc, Vector3f Gyr) {
    Vector3f Vel(0, 0, 0);
    Vector3f Pos(0, 0, 0);
        float pi = 2 * asin(1.0);
        float Rx = Gyr[0];//pitch
        float Ry = Gyr[1];//roll
        float Rz = Gyr[2]; //yaw
        bool run = true;
        Matrix3f Rtotal{
            { cos(Rz) * cos(Rx),                                    sin(Rz) * cos(Rx),                                      -sin(Rx) },
            { cos(Rz) * sin(Rx) * sin(Ry) - cos(Ry) * sin(Rz),      cos(Ry) * cos(Rz) + sin(Ry) * sin(Rz) + sin(Rx),        cos(Rx) * sin(Ry) },
            { sin(Ry) * sin(Rz) + cos(Ry) * cos(Rz) * sin(Rx),      cos(Rz) * sin(Rx) * sin(Ry) - sin(Ry) * cos(Rz),        cos(Rx) * cos(Ry) }
        };
        float Ax = 0.0;
        float Ay = 0.0;
        float Az = 1.0; // the accelerations to be input from the IMU in g
        MatrixXf Acccart(Rtotal.inverse() * Acc); // finds the accelleration in cartesian plane

        
        package mypackage(Acccart, Vel, Pos, Rtotal, Ry, Rx, Rz);
        mypackage.acceleration[2] = mypackage.acceleration[2];
        mypackage.velocity += mypackage.acceleration;
        mypackage.position += mypackage.velocity;
        return mypackage;
};
bool output(package mypackage){
    cout << "accelerating at\n" << mypackage.acceleration[0] << "g in the direction x \n" << mypackage.acceleration[1] << "g in the direction y \n" << mypackage.acceleration[2] << "g in the direction z \n" << endl;
    cout << "moving at\n" << mypackage.velocity[0] << "gs in the direction x \n" << mypackage.velocity[1] << "gs in the direction y \n" << mypackage.velocity[2] << "gs in the direction z \n" << endl;
    cout << "located at\n" << mypackage.position[0] << "gss in the direction x \n" << mypackage.position[1] << "gss in the direction y \n" << mypackage.position[2] << "gss in the direction z \n" << endl;
    sleep_for(chrono::seconds(1));
    return 0;
};
int main(){
    bool run = true;
    imu_sample_t s;
    string port = find_port();
    int i;
    int fd = imu_open(port.c_str());                 // fd, or -1
    if (fd < 0) { std::cerr << "Pico not found\n"; return 1; }
    while (run == true){
        int r = imu_read(fd, &s, 1000);              // 1 = sample, 0 = timeout, -1 = unplugged
        Vector3f acc(s.a[0] / 16384.0f, s.a[1] / 16384.0f, s.a[2] / 16384.0f);   // g
        Vector3f gyr(s.g[0] / 131.0f, s.g[1] / 131.0f, s.g[2] / 131.0f);         // deg/s
        package mypackage = getaccelerations(acc,gyr);
        i+=5;
        i = i%6;
        if (i ==5){
            output(mypackage);
        };
        
        
    };

    
};



/*
while (true) {
        int r = imu_read(fd, &s, 1000);              // 1 = sample, 0 = timeout, -1 = unplugged
        if (r < 0) break;
        if (r == 0) continue;

        // Raw counts -> physical units.
        Vector3f acc(s.a[0] / 16384.0f, s.a[1] / 16384.0f, s.a[2] / 16384.0f);   // g
        Vector3f gyr(s.g[0] / 131.0f, s.g[1] / 131.0f, s.g[2] / 131.0f);         // deg/s

        // Time since the previous sample, from the Pico's own clock (about 0.005 s).
        float dt = first ? 0.005f : (uint32_t)(s.t_us - last_t) * 1e-6f;
        last_t = s.t_us; first = false;

        // Roll and pitch from gravity (only valid while the device is not accelerating).
        float roll  = std::atan2(acc.y(), acc.z());
        float pitch = std::atan2(-acc.x(), std::sqrt(acc.y() * acc.y() + acc.z() * acc.z()));
        float yaw   = 0;                             // gravity says nothing about yaw
        Matrix3f R = (AngleAxisf(yaw, Vector3f::UnitZ()) * AngleAxisf(pitch, Vector3f::UnitY()) *
                      AngleAxisf(roll, Vector3f::UnitX())).toRotationMatrix();   // sensor -> world

        Vector3f acc_world = R * acc;                // g, world frame
        acc_world.z() -= 1.0f;                       // remove gravity
        vel += acc_world * 9.81f * dt;               // m/s    (v = v + a dt)
        pos += vel * dt;                             // m      (s = s + v dt)

        if (s.seq % 200 == 0) {                      // print once a second
            std::cout << "acc [g] " << acc.transpose() << "   gyro [deg/s] " << gyr.transpose()
                      << "   roll " << roll * 57.3f << " pitch " << pitch * 57.3f
                      << "   pos [m] " << pos.transpose() << std::endl;
        }
    }
    imu_close(fd);
    return 0;
}
*/
