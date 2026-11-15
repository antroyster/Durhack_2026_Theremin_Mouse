#include <Eigen\Eigen>
#include <iostream>
using namespace std;
using namespace Eigen;
int main() {
    double pi = 2*asin(1.0);
    double Ry = 0 ;//roll
    double Rx = 0 ;//pitch
    double Rz = 0; //yaw
    Matrix3f Rtotal {
        {cos(Rz)*cos(Rx), sin(Rz)*cos(Rx), -sin(Rx)},
        {cos(Rz)*sin(Rx)*sin(Ry)-cos(Ry)*sin(Rz), cos(Ry)*cos(Rz)+sin(Ry)*sin(Rz)+sin(Rx), cos(Rx)*sin(Ry)},
        {sin(Ry)*sin(Rz)+cos(Ry)*cos(Rz)*sin(Rx), cos(Rz)*sin(Rx)*sin(Ry)-sin(Ry)*cos(Rz),Cos(Rx)*cos(Ry)}
    };
    double Ax = 0;
    double Ay = 0;
    double Az = 1
    Matrix<double, 1,3> Acc {
        {Ax},
        {Ay},
        {Az}
    };
    MatrixXf Acccart (Acc*Rtotal.inverse);
     cout << "Here is the vector Acc \n" << Acc <<"\n here is the vector Acccart \n" << Acccart <<endl;
};
