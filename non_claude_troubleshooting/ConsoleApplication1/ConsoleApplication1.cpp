// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
# include </home/anthony-royster/Durhack_2026_Theremin_Mouse/non_claude/eigen-5.0.1/Eigen/Eigen>
#include <iostream>
#include <thread>
#include <chrono>

using namespace Eigen;
using namespace std;

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
package getaccelerations() {
    Vector3f Vel(0, 0, 0);
    Vector3f Pos(0, 0, 0);
    while (true) {
        float pi = 2 * asin(1.0);
        float Ry = 0;//roll
        float Rx = 0;//pitch
        float Rz = 0; //yaw
        bool run = true;
        Matrix3f Rtotal{
            { cos(Rz) * cos(Rx),                                    sin(Rz) * cos(Rx),                                      -sin(Rx) },
            { cos(Rz) * sin(Rx) * sin(Ry) - cos(Ry) * sin(Rz),      cos(Ry) * cos(Rz) + sin(Ry) * sin(Rz) + sin(Rx),        cos(Rx) * sin(Ry) },
            { sin(Ry) * sin(Rz) + cos(Ry) * cos(Rz) * sin(Rx),      cos(Rz) * sin(Rx) * sin(Ry) - sin(Ry) * cos(Rz),        cos(Rx) * cos(Ry) }
        };
        float Ax = 0.0;
        float Ay = 0.0;
        float Az = 1.0; // the accelerations to be input from the IMU in g
        Vector3f Acc(Ax, Ay, Az);
        MatrixXf Acccart(Rtotal.inverse() * Acc); // finds the accelleration in cartesian plane

        
        package mypackage(Acccart, Vel, Pos, Rtotal, Ry, Rx, Rz);
        mypackage.acceleration[2] = mypackage.acceleration[2]-1;
        mypackage.velocity += mypackage.acceleration;
        mypackage.position += mypackage.velocity;
        cout << "accelerating at\n" << mypackage.acceleration[0] << "g in the direction x \n" << mypackage.acceleration[1] << "g in the direction y \n" << mypackage.acceleration[2] << "g in the direction z \n" << endl;
        cout << "moving at\n" << mypackage.velocity[0] << "gs in the direction x \n" << mypackage.velocity[1] << "gs in the direction y \n" << mypackage.velocity[2] << "gs in the direction z \n" << endl;
        cout << "located at\n" << mypackage.position[0] << "gss in the direction x \n" << mypackage.position[1] << "gss in the direction y \n" << mypackage.position[2] << "gss in the direction z \n" << endl;
        this_thread::sleep_for(chrono::seconds(1));
    };
};

int main(){
    bool run = true;
    
    while (run == true){
        
        package mypackage = getaccelerations();
        
        cout << "accelerating at\n" << mypackage.acceleration[0] << "g in the direction x \n" << mypackage.acceleration[1] << "g in the direction y \n" << mypackage.acceleration[2] << "g in the direction z \n" << endl;
        cout << "moving at\n" << mypackage.velocity[0] << "gs in the direction x \n" << mypackage.velocity[1] << "gs in the direction y \n" << mypackage.velocity[2] << "gs in the direction z \n" << endl;
        cout << "located at\n" << mypackage.position[0] << "gss in the direction x \n" << mypackage.position[1] << "gss in the direction y \n" << mypackage.position[2] << "gss in the direction z \n" << endl;
        this_thread::sleep_for(chrono::seconds(1));
    };

    
};
