#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"

/*
                    +------[ USB ]------+
          GP0  [ 1] |o                 o| [40] VBUS
          GP1  [ 2] |o                 o| [39] VSYS
          GND  [ 3] |o                 o| [38] GND  <--- GY-521 GND
          GP2  [ 4] |o                 o| [37] 3V3_EN
          GP3  [ 5] |o                 o| [36] 3V3(OUT)  <--- GY-521 VCC
          GP4  [ 6] |o                 o| [35] ADC_VREF
          GP5  [ 7] |o                 o| [34] GP28
          GND  [ 8] |o                 o| [33] GND
          GP6  [ 9] |o                 o| [32] GP27
          GP7  [10] |o                 o| [31] GP26
 SDA ---> GP8  [11] |o                 o| [30] RUN
 SCL ---> GP9  [12] |o                 o| [29] GP22
          GND  [13] |o                 o| [28] GND
          GP10 [14] |o                 o| [27] GP21
          GP11 [15] |o                 o| [26] GP20
          GP12 [16] |o                 o| [25] GP19
          GP13 [17] |o                 o| [24] GP18
          GND  [18] |o                 o| [23] GND
          GP14 [19] |o                 o| [22] GP17
          GP15 [20] |o                 o| [21] GP16
                    +-------------------+
*/
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define MPU_ADDR         0x68
#define REG_WHO_AM_I     0x75
#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_XOUT_H 0x3B
#define POLL_TIME 100 //ms
#define A_UNIT 16384.0f //ms^-2
#define G_UNIT 131.0f // rad^-1
#define T_UNIT_DIV 340.0f
#define T_UNIT_ADD 36.53f // deg c 

static int mpu_write_reg(uint8_t reg, uint8_t value){
    uint8_t buf[2] = {reg,value};
    return i2c_write_blocking(I2C_PORT, MPU_ADDR, buf,2,false);
}

static int mpu_read_regs(uint8_t reg, uint8_t *dst, size_t len) {
    int r = i2c_write_blocking(I2C_PORT, MPU_ADDR, &reg, 1, true);
    if (r < 0) return r; //negative failed value
    return i2c_read_blocking(I2C_PORT, MPU_ADDR, dst, len, false);
}
static float data_output_processing(uint8_t hi, uint8_t lo, uint8_t type) {
    int16_t raw = (int16_t)(((uint16_t)hi << 8) | lo);

    switch (type) {
        case 0:
            return raw / A_UNIT;
        case 1:
            return raw / G_UNIT;
        case 2:
            return raw / T_UNIT_DIV + T_UNIT_ADD;
        default:
            return 0.0f;
    }
}
int debug_loop_for_serial_monitor(){
    for(uint8_t i=0; i<10 ; i++)
    {    sleep_ms(1000);
        printf("Serial Output \n");
    }
    return 0;
}
int debug_initial(){
    stdio_init_all();
    debug_loop_for_serial_monitor();
    gpio_init(I2C_SDA); gpio_set_dir(I2C_SDA, GPIO_IN); gpio_disable_pulls(I2C_SDA);
    gpio_init(I2C_SCL); gpio_set_dir(I2C_SCL, GPIO_IN); gpio_disable_pulls(I2C_SCL);
    sleep_ms(10);
    printf("No internal pull-ups: SDA=%d SCL=%d\n", gpio_get(I2C_SDA), gpio_get(I2C_SCL));

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400*1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    printf("Scanning I2C bus\n");
    for (uint8_t addr=0x08; addr <= 0x77; addr++)
    {
        uint8_t dummy;
        int result = i2c_read_blocking(I2C_PORT, addr, &dummy,1,false);
        if (result >= 0)
        {
            printf("Found Device at 0x%02x\n",addr);
        }
    }
    printf("Scan Done \n");
    while(true) sleep_ms(1000);
    return 0;
}
int debug_comparitor_read(){
    uint8_t id = 0;
    if (mpu_read_regs(REG_WHO_AM_I, &id, 1) < 0) {
        printf("No response from MPU-6050, check wiring.\n");
    } else {
        printf("WHO_AM_I = 0x%02X (expect 0x68)\n", id);
    }
    return 0;

}
int wake_up_sensor(){
    mpu_write_reg(REG_PWR_MGMT_1, 0x00);   // clears the sleepy bit
    sleep_ms(100);    
    return 0;
}
int polling_loop(float *ax,float*ay,float*az,float*t,float*gx,float*gy,float*gz){
    uint8_t raw[14];
        if (mpu_read_regs(REG_ACCEL_XOUT_H, raw, sizeof raw) < 0) {
            printf("Read failed\n");
            sleep_ms(500);
        }
        else{
            *ax = data_output_processing(raw[0],  raw[1],0);  
            *ay = data_output_processing(raw[2],  raw[3],0) ; 
            *az = data_output_processing(raw[4],  raw[5],0)  ;
            *t  = data_output_processing(raw[6],  raw[7],2)  ;
            *gx = data_output_processing(raw[8],  raw[9],1)  ;
            *gy = data_output_processing(raw[10], raw[11],1) ;
            *gz = data_output_processing(raw[12], raw[13],1) ;
        }
        printf("Accel[g] %6.2f %6.2f %6.2f | Gyro[dps] %7.1f %7.1f %7.1f | %.1fC\n",
               *ax, *ay, *az, *gx, *gy, *gz, *t);
}
int setup_initial(){

    stdio_init_all();
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    return 0;
}
int main()
{
    float ax,ay,az,t,gx,gy,gz;
    absolute_time_t next = get_absolute_time();
    setup_initial();
    debug_loop_for_serial_monitor();
    debug_comparitor_read();
    wake_up_sensor();
    while(true)
    {
        polling_loop(&ax, &ay, &az, &t, &gx, &gy, &gz);
        next = delayed_by_us(next, POLL_TIME * 10);   // advance from the last deadline
        sleep_until(next);
    }
}
