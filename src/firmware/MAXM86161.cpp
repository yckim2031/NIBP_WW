#include "MAXM86161.h"
#include "stdlib.h"

/*****************************************************************************/
// Constructor
/*****************************************************************************/
/*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*/
MAXM86161::MAXM86161(I2C &i2c):
_i2cbus(i2c)
{
    // Empty block
}

MAXM86161::~MAXM86161(void)
{
    // Empty block
}


// Initialize the sensor
// Sets the PPG sensor to starting condition, then puts it in SHDN mode ready to
// take data
int MAXM86161::init(void)
{
    int read_value;
    // Use function to do software reset
    _write_to_reg(REG_SYSTEM_CONTROL, 0x09);

    ThisThread::sleep_for(1ms);

    // Shut Down
    _write_to_reg(REG_SYSTEM_CONTROL, 0x0A);

    ThisThread::sleep_for(2ms);

    // Clear Interrupt 1 by reading
    _read_from_reg(REG_IRQ_STATUS1, read_value);

    // Clear Interrupt 2 by reading
    _read_from_reg(REG_IRQ_STATUS2, read_value);

    // Set integration time and ADC range with ALC and ADD
    _write_to_reg(REG_PPG_CONFIG1, 0x0F);

    // Set sample rate and averaging
    // cmd[0] = 0x12; cmd[1] = 0x50;  // 8Hz with no averaging
    // cmd[1] = 0x08; 50 Hz with no averaging
    _write_to_reg(REG_PPG_CONFIG2, 0x80);

    // Set LED settling, digital filter, burst rate, burst enable
    //  No Burst mode with default settling
    _write_to_reg(REG_PPG_CONFIG3, 0x40);

    // Set Photodiode bias to 0pF to 65pF
    _write_to_reg(REG_PD_BIAS, 0x40);

    _write_to_reg(REG_LED_RANGE1, 0x3F);                // Set LED driver range to 124 mA

    set_all_led_current(0x11);                          // Set LED current

    _write_to_reg(REG_SYSTEM_CONTROL, 0xC);             // Enable Low Power Mode

    //********************************
    // FIFO enable

    
    _write_to_reg(REG_FIFO_CONFIG1, 0x7C);              // Set FIFO full to 64 empty spaces left

    _write_to_reg(REG_FIFO_CONFIG2, 0b00001010);       // Enable FIFO rollover when full

    _write_to_reg(REG_IRQ_ENABLE1, 0b11000000);         // Enable interrupt when new sample & when fifo is soon be fully filled

    // Set LED exposure to timeslots
    _write_to_reg(REG_LED_SEQ1, 0x01);                  // LEDC1: Green
    _write_to_reg(REG_LED_SEQ2, 0x00);                  // LEDC3~6 : None
    _write_to_reg(REG_LED_SEQ3, 0x00);

    // Shutdown at the end and wait for signal to start
    stop();

    // Read device ID, if it matches the value for MAXM86161, return 0, otherwise return 1.
    _write_to_reg(REG_PART_ID, read_value);

    _clear_interrupt();

    if(read_value == PPG_PART_ID){
        return 0;
    }

    else{
        return 1;
    }
}

int MAXM86161::start(void)
{
    int existing_reg_values;
    int status;
    // Get value of register
    _read_from_reg(REG_SYSTEM_CONTROL, existing_reg_values);

    // Clear the bit to start the device
    existing_reg_values = _clear_one_bit(existing_reg_values, POS_START_STOP);

    // Write to the register to start the device
    status = _write_to_reg(REG_SYSTEM_CONTROL, existing_reg_values);

    return status;
}

int MAXM86161::stop(void)
{
    int existing_reg_values;
    int status;

    // Get value of register
    _read_from_reg(REG_SYSTEM_CONTROL, existing_reg_values);

    // Set the bit to stop the device
    existing_reg_values = _set_one_bit(existing_reg_values, POS_START_STOP); 
    
    // Send the Shutdown command to the device
    status = _write_to_reg(REG_SYSTEM_CONTROL, existing_reg_values);
    return status;
}

int MAXM86161::read(int &red, int &green, int &ir, int&ambient, int read_position, int write_position)
{
    int status;
    char databuffer[128*BYTES_PER_CH];
    int number_of_bytes;
    int led1;
    int led2;
    int led3;
    int led4;
    char cmd[16];

    if (write_position > read_position){
        number_of_bytes = ((write_position - read_position) * (BYTES_PER_CH * LED_NUM));
    }
    else{
        number_of_bytes = (((FIFO_SIZE - read_position) + write_position) * (BYTES_PER_CH * LED_NUM));
    }

    // Read the FIFO sample
    cmd[0] = REG_FIFO_DATA;
    _i2cbus.write(PPG_ADDR, cmd, 1, true);
    status = _i2cbus.read(PPG_ADDR, databuffer, number_of_bytes);

    // Parse the FIFO data
    led1 = (databuffer[0] << 16) | (databuffer[1] << 8) | (databuffer[2]) ;
    led2 = (databuffer[3] << 16) | (databuffer[4] << 8) | (databuffer[5]) ;
    led3 = (databuffer[6] << 16) | (databuffer[7] << 8) | (databuffer[8]) ;
    led4 = (databuffer[9] << 16) | (databuffer[10] << 8) | (databuffer[11]) ;

    red = led1&MASK_PPG_LABEL;
    green = led2&MASK_PPG_LABEL;
    ir = led3&MASK_PPG_LABEL;
    ambient = led4&MASK_PPG_LABEL;

    _clear_interrupt();
    return status;
    
}


/*******************************************************************************/
int MAXM86161::set_interrogation_rate(int rate)
{
    int existing_reg_values;
    int status;

    // Get value of register to avoid overwriting sample average value
    _read_from_reg(REG_PPG_CONFIG2, existing_reg_values);

    // Set the appropriate bits, while leaving the others.
    existing_reg_values = _set_multiple_bits(existing_reg_values, MASK_SMP_AVE, rate, POS_PPG_SR);

    status = _write_to_reg(REG_PPG_CONFIG2, existing_reg_values);
    return status;
}

int MAXM86161::set_sample_averaging(int average)
{
    int existing_reg_values;
    int status;

    // Get value of register to avoid overwriting sample average value
    _read_from_reg(REG_PPG_CONFIG2, existing_reg_values);

    // Set the appropriate bits, while leaving the others.
    existing_reg_values = _set_multiple_bits(existing_reg_values, MASK_PPG_SR, average, POS_SMP_AVG);

    status = _write_to_reg(REG_PPG_CONFIG2, existing_reg_values);
    return status;
}

int MAXM86161::set_all_led_current(int current)
{
    int status_1;
    int status_2;
    int status_3;
    int status_total;

    // Set each LED current
    status_1 = set_led1_current(current);
    status_2 = set_led2_current(current);
    status_3 = set_led3_current(current);
    // Return the sum of the status values.
    // Will be zero for sucessful writing of LED currents.
    status_total = status_1 + status_2 + status_3;
    return status_total;
}


int MAXM86161::set_led1_current(int current)
{
    int status;
    status = _write_to_reg(REG_LED1_PA, current);
    return status;
}

int MAXM86161::set_led2_current(int current)
{
    int status;
    status = _write_to_reg(REG_LED2_PA, current);
    return status;
}

int MAXM86161::set_led3_current(int current)
{
    int status;
    status = _write_to_reg(REG_LED3_PA, current);
    return status;
}

int MAXM86161::set_ppg_tint(int time)
{
    int existing_reg_values;
    int status;

    // Get value of register to avoid overwriting sample average value
    _read_from_reg(REG_PPG_CONFIG1, existing_reg_values);

    // Set the appropriate bits, while leaving the others.
    existing_reg_values = _set_multiple_bits(existing_reg_values, MASK_PPG_TINT_WRITE, time, POS_PPG_TINT);

    status = _write_to_reg(REG_PPG_CONFIG1, existing_reg_values);
    return status;

}



/*******************************************************************************/
int MAXM86161::alc_on(void)
{
    int existing_reg_values;
    int status;

    // Get value of register
    _read_from_reg(REG_PPG_CONFIG1, existing_reg_values);

    // Set the bit to stop the device
    existing_reg_values = _clear_one_bit(REG_PPG_CONFIG1, POS_ALC_DIS); 
    
    // Send the Shutdown command to the device
    status = _write_to_reg(REG_PPG_CONFIG1, existing_reg_values);
 
    return status;
}

int MAXM86161::alc_off(void)
{
    int existing_reg_values;
    int status;

    // Get value of register
    _read_from_reg(REG_PPG_CONFIG1, existing_reg_values);

    // Set the bit to stop the device
    existing_reg_values = _set_one_bit(REG_PPG_CONFIG1, POS_ALC_DIS); 
    
    // Send the Shutdown command to the device
    status = _write_to_reg(REG_PPG_CONFIG1, existing_reg_values);
 
    return status;
}


int MAXM86161::picket_off(void)
{
    int existing_reg_values;
    int status;

    // Get value of register
    _read_from_reg(REG_PICKET_FENCE, existing_reg_values);

    // Set the bit to stop the device
    existing_reg_values = _clear_one_bit(REG_PICKET_FENCE, POS_PICKET_DIS); 
    
    // Send the Shutdown command to the device
    status = _write_to_reg(REG_PICKET_FENCE, existing_reg_values);
 
    return status;
}

int MAXM86161::picket_on(void)
{
    int existing_reg_values;
    int status;

    // Get value of register
    _read_from_reg(REG_PICKET_FENCE, existing_reg_values);

    // Set the bit to stop the device
    existing_reg_values = _set_one_bit(REG_PICKET_FENCE, POS_PICKET_DIS); 
    
    // Send the Shutdown command to the device
    status = _write_to_reg(REG_PICKET_FENCE, existing_reg_values);
 
    return status;
}

int MAXM86161::new_value_read_on(void)
{
    int existing_reg_values;
    int status;

    // Get value of register
    _read_from_reg(REG_IRQ_ENABLE1, existing_reg_values);

    // Set the bit to stop the device
    existing_reg_values = _set_one_bit(REG_IRQ_ENABLE1, POS_DATA_RDY_EN); 
    
    // Send the Shutdown command to the device
    status = _write_to_reg(REG_IRQ_ENABLE1, existing_reg_values);
 
    return status;
}

int MAXM86161::new_value_read_off(void)
{
    int existing_reg_values;
    int status;

    // Get value of register
    _read_from_reg(REG_IRQ_ENABLE1, existing_reg_values);

    // Set the bit to stop the device
    existing_reg_values = _clear_one_bit(REG_IRQ_ENABLE1, POS_DATA_RDY_EN); 
    
    // Send the Shutdown command to the device
    status = _write_to_reg(REG_IRQ_ENABLE1, existing_reg_values);
 
    return status;
}



/*******************************************************************************/
// Function to read from a registry
int MAXM86161::_read_from_reg(int address, int &data){

    char cmd[16];
    char rsp[256];
    int status;
    cmd[0] = address;
    status = _i2cbus.write(PPG_ADDR, cmd, 1, true);
    if(status !=0) {
        // serial_pc.printf("Failed to write register address %#X during read.\n", address);
        return status;
        }
    
    status = _i2cbus.read(PPG_ADDR, rsp, 1);
    if(status !=0){        
        // serial_pc.printf("Failed to read register %#X value during read.\n", address);

        return status;
        }

    data = rsp[0];
    // serial_pc.printf("\n\r %#X Status: %#X\n\r", address, rsp[0]);
    return status;
}

// Function to write to a registry
// TODO Check about mfio -> might not need it.
int MAXM86161::_write_to_reg(int address, int value) {
    char cmd[16];
    char rsp[256];
    int status;
    cmd[0] = address; cmd[1] = value;
    status = _i2cbus.write(PPG_ADDR, cmd, 2);
    if(status !=0) {
        // serial_pc.printf("write error %#x\n", address);
        }
    else {
        _i2cbus.read(PPG_ADDR, rsp, 1);
    }
    return status;
}

int MAXM86161::_set_one_bit(int current_bits, int position)
{
    current_bits = current_bits | 1 << position;
    return  current_bits;
}

int MAXM86161::_clear_one_bit(int current_bits, int position)
{
    current_bits = current_bits & ~(1 << position);
    return current_bits;
}

int MAXM86161::_set_multiple_bits(int current_bits, int mask, int new_value, int position)
{
    current_bits = (current_bits & mask) | (new_value << position);
    return current_bits;
}

int MAXM86161::_clear_interrupt(void)
{
    int status;
    int value;
    status = _read_from_reg(REG_IRQ_STATUS1, value);
    return status;
}

// int MAXM86161::_get_data_label(int &data, int &label, char[int] &raw)
// {
//     int shifted_value = 0;

//     // Shift the data bits
//     shifted_value = (raw[0] << 16) | (raw[1] << 8) | raw[2];
    
//     // Apply the mask to get the data
//     data = shifted_value & MASK_PPG_LABEL;
//     label = raw[0] & MASK_PPG_ID;

//     return 0;
// }

/*
// Online C++ compiler to run C++ program online
#include <iostream>

#define MASK_PPG_LABEL 0x7FFFF;
#define MASK_PPG_ID 0xF8;


int get_data_label(int &data, int &label, int &raw1, int &raw2, int &raw3)
{
    int shifted_value = 0;

    // Shift the data bits
    shifted_value = (raw1 << 16) | (raw2 << 8) | raw3;
    
    // Apply the mask to get the data
    data = shifted_value & MASK_PPG_LABEL;
    
    label = raw1 & MASK_PPG_ID;
    label = label >> 3;
    
    return 0;
}


int main() {
    int stream_data0;
        int stream_data1;
    int stream_data2;    
    int ppg_data = 0;
    int ppg_id = 0;
    // Write C++ code here
    stream_data0 = 0b00001001;
    stream_data1 = 0b10010010;
    stream_data2 = 0b01011000;
    get_data_label(ppg_data, ppg_id, stream_data0, stream_data1, stream_data2);
    
    std::cout << "Data:" << ppg_data << " ID:" << ppg_id << "\n";

    return 0;
}
*/
