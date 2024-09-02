/* 
    This Sensor-Statemachine for SML-parsing was taken from M. Ruettgers great project SMLReader https://github.com/mruettgers/SMLReader
*/



#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <SerialDebug.h>


using namespace std;

// SML constants
const byte START_SEQUENCE[] = {0x1B, 0x1B, 0x1B, 0x1B, 0x01, 0x01, 0x01, 0x01};
const byte END_SEQUENCE[] = {0x1B, 0x1B, 0x1B, 0x1B, 0x1A};
const size_t BUFFER_SIZE = 3840; // Max datagram duration 400ms at 9600 Baud
const uint8_t READ_TIMEOUT = 30;

// States
enum State{
    INIT,
    STANDBY_TILL_RETRY,
    WAIT_FOR_START_SEQUENCE,
    READ_MESSAGE,
    PROCESS_MESSAGE,
    READ_CHECKSUM,
    FINISHED
};

uint64_t millis64(){
    static uint32_t low32, high32;
    uint32_t new_low32 = millis();
    if (new_low32 < low32)
        high32++;
    low32 = new_low32;
    return (uint64_t)high32 << 32 | low32;
}


class SensorConfig{
public:
    const uint8_t pin;
    const bool numeric_only;
};

class Sensor{
public:
    const SensorConfig *config;
    Sensor(const SensorConfig *config, boolean (*callback)(byte *buffer, size_t len, Sensor *sensor)){
        this->config = config;
        debugD("Initializing sensor at pin %i...", this->config->pin);
        this->callback = callback;
        this->serial = unique_ptr<SoftwareSerial>(new SoftwareSerial());
        this->serial->begin(9600, SWSERIAL_8N1, this->config->pin, -1, false);
        this->serial->enableTx(false);
        this->serial->enableRxGPIOPullUp(false);
        this->serial->enableRx(true);
        debugD("Initialized sensor at pin %i.", this->config->pin);

        this->init_state();
    }

    void loop(){
        this->run_current_state();
        yield();
    }

private:
    unique_ptr<SoftwareSerial> serial;
    byte buffer[BUFFER_SIZE];
    size_t position = 0;
    unsigned long last_state_reset = 0;
    uint64_t standby_until = 0;
    uint8_t bytes_until_checksum = 0;
    uint8_t loop_counter = 0;
    State state = INIT;
    boolean (*callback)(byte *buffer, size_t len, Sensor *sensor) = NULL;

    void run_current_state(){
        if (this->state != INIT){
            if (this->state != STANDBY_TILL_RETRY && ((millis() - this->last_state_reset) > (READ_TIMEOUT * 1000))){
                debugD("Did not receive an SML message within %d seconds, starting over.", READ_TIMEOUT);
                this->reset_state();
            }
            switch (this->state){
            case STANDBY_TILL_RETRY:
                this->standby_till_retry();
                break;
            case WAIT_FOR_START_SEQUENCE:
                this->wait_for_start_sequence();
                break;
            case READ_MESSAGE:
                this->read_message();
                break;
            case PROCESS_MESSAGE:
                this->process_message();
                break;
            case READ_CHECKSUM:
                this->read_checksum();
                break;
            default:
                break;
            }
        }
    }

    // Wrappers for sensor access
    int data_available(){
        return this->serial->available();
    }

    int data_read(){
        return this->serial->read();
    }

    // Set state
    void set_state(State new_state){
        if (new_state == STANDBY_TILL_RETRY){
            debugD("State of sensor pin %i is 'STANDBY'.", this->config->pin);
        }else if (new_state == WAIT_FOR_START_SEQUENCE){
            debugD("State of sensor pin %i is 'WAIT_FOR_START_SEQUENCE'.", this->config->pin);
            this->last_state_reset = millis();
            this->position = 0;
        }else if (new_state == READ_MESSAGE){
            debugD("State of sensor pin %i is 'READ_MESSAGE'.", this->config->pin);
        }else if (new_state == READ_CHECKSUM){
            debugD("State of sensor pin %i is 'READ_CHECKSUM'.", this->config->pin);
            this->bytes_until_checksum = 3;
        }else if (new_state == PROCESS_MESSAGE){
            debugD("State of sensor pin %i is 'PROCESS_MESSAGE'.", this->config->pin);
        }else if (new_state == FINISHED){
            debugD("shit is done, wait for sleep");
        };
        this->state = new_state;
    }

    // Initialize state machine
    void init_state(){
        this->set_state(WAIT_FOR_START_SEQUENCE);
    }

    // Start over and wait for the start sequence
    void reset_state(const char *message = NULL){
        if (message != NULL && strlen(message) > 0){
            printD(message);
        }
        this->init_state();
    }

    void standby_till_retry(){
        // Keep buffers clean
        while (this->data_available()){
            this->data_read();
            yield();
        }

        if ( millis64() >= this->standby_until){
            this->reset_state();
        }
    }

    // Wait for the start_sequence to appear
    void wait_for_start_sequence(){
        while (this->data_available()){
            this->buffer[this->position] = this->data_read();
            yield();

            this->position = (this->buffer[this->position] == START_SEQUENCE[this->position]) ? (this->position + 1) : 0;
            if (this->position == sizeof(START_SEQUENCE)){
                // Start sequence has been found
                printD("Start sequence found.");
                this->set_state(READ_MESSAGE);
                return;
            }
        }
    }

    // Read the rest of the message
    void read_message(){
        while (this->data_available()){
            // Check whether the buffer is still big enough to hold the number of fill bytes (1 byte) and the checksum (2 bytes)
            if ((this->position + 3) == BUFFER_SIZE){
                this->reset_state("Buffer will overflow, starting over.");
                return;
            }
            this->buffer[this->position++] = this->data_read();
            yield();

            // Check for end sequence
            int last_index_of_end_seq = sizeof(END_SEQUENCE) - 1;
            for (int i = 0; i <= last_index_of_end_seq; i++){
                if (END_SEQUENCE[last_index_of_end_seq - i] != this->buffer[this->position - (i + 1)]){
                    break;
                }
                if (i == last_index_of_end_seq){
                    debugD("End sequence found.");
                    this->set_state(READ_CHECKSUM);
                    return;
                }
            }
        }
    }

    // Read the number of fillbytes and the checksum
    void read_checksum(){
        while (this->bytes_until_checksum > 0 && this->data_available()){
            this->buffer[this->position++] = this->data_read();
            this->bytes_until_checksum--;
            yield();
        }

        if (this->bytes_until_checksum == 0){
            debugD("Message has been read.");
            //DEBUG_DUMP_BUFFER(this->buffer, this->position);
            this->set_state(PROCESS_MESSAGE);
        }
    }

    void process_message(){
        debugD("Message is being processed.");
        
        // Call listener
        if (this->callback != NULL){
            boolean r = this->callback(this->buffer, this->position, this);
            if(r == true){
                this->set_state(FINISHED);
            }else{
                this->standby_until = millis64() + (5 * 1000);
                this->set_state(STANDBY_TILL_RETRY);
            }

        }

        
    }
};

#endif
