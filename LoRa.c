#include "LoRa.h"

    //TODO применить проверку соединения.
int LoRa_begin(LoRa_ctl *modem){
    if (gpioInitialise() < 0)
    {
        printf("Pigpio init error\n");
        return 0;
    }
    
    lora_reset(modem->eth.resetGpioN);
    
    if( (modem->spid = spiOpen(modem->spiCS, 32000, 0))<0 )
        return modem->spid;
    
    lora_set_lora_mode(modem->spid);
    
    if(modem->eth.implicitHeader){
        lora_set_implicit_header(modem->spid);
        lora_set_payload(modem->spid, modem->eth.payloadLen);
    }
    else{
        lora_set_explicit_header(modem->spid);
    }
    
    lora_set_errorcr(modem->spid, modem->eth.ecr);
    lora_set_bandwidth(modem->spid, modem->eth.bw);
    lora_set_sf(modem->spid, modem->eth.sf);
    lora_set_crc_on(modem->spid);
    lora_set_tx_power(modem->spid, modem->eth.outPower, modem->eth.powerOutPin);
    lora_set_syncw(modem->spid, modem->eth.syncWord);
    lora_set_preamble(modem);
    lora_set_agc(modem);
    lora_set_lna(modem);
    lora_set_ocp(modem);
    
    lora_reg_write_byte(modem->spid, REG_FIFO_TX_BASE_ADDR, TX_BASE_ADDR);
    lora_reg_write_byte(modem->spid, REG_FIFO_RX_BASE_ADDR, RX_BASE_ADDR);
    lora_reg_write_byte(modem->spid, REG_DETECT_OPTIMIZE, 0xc3);//LoRa Detection Optimize for SF > 6
    lora_reg_write_byte(modem->spid, REG_DETECTION_THRESHOLD, 0x0a);//DetectionThreshold for SF > 6
    
    lora_set_freq(modem->spid, modem->eth.freq);
    return modem->spid;
}

void lora_set_ocp(LoRa_ctl *modem){
    unsigned char OcpTrim;
    if(modem->eth.OCP == 0){//turn off OCP
        lora_reg_write_byte(modem->spid, REG_OCP, (lora_reg_read_byte(modem->spid, REG_OCP) & 0xdf));
    }
    else if(modem->eth.OCP > 0 && modem->eth.OCP <= 120){
        if(modem->eth.OCP < 50){modem->eth.OCP = 50;}
        
        OcpTrim = (modem->eth.OCP-45)/5 + 0x20;
        lora_reg_write_byte(modem->spid, REG_OCP, OcpTrim);
    }
    else if(modem->eth.OCP > 120){
        if(modem->eth.OCP < 130){modem->eth.OCP = 130;}
        
        OcpTrim = (modem->eth.OCP+30)/10 + 0x20;
        lora_reg_write_byte(modem->spid, REG_OCP, OcpTrim);
    }
}

void lora_set_lna(LoRa_ctl *modem){
    lora_reg_write_byte(modem->spid, REG_LNA,  ( (modem->eth.lnaGain << 5) + modem->eth.lnaBoost) );
}

void lora_set_agc(LoRa_ctl *modem){
    lora_reg_write_byte(modem->spid, REG_MODEM_CONFIG_3, (modem->eth.AGC << 2));
}

void lora_set_tx_power(int spid, OutputPower power, PowerAmplifireOutputPin pa_pin){
    lora_reg_write_byte(spid, REG_OCP, 0x1f);//Disable over current protection
    
    if(pa_pin == RFO){
        power = power >= OP15 ? OP15 : ( power <= OP0 ? OP0 : power);
        lora_reg_write_byte(spid, REG_PA_DAC, 0x84);//default val to +17dBm
        lora_reg_write_byte(spid, REG_PA_CONFIG, pa_pin | power);
        return;
    }else if(pa_pin == PA_BOOST){
        if(power == OP20){
            lora_reg_write_byte(spid, REG_PA_DAC, 0x87);//Max 20dBm power
            lora_reg_write_byte(spid, REG_PA_CONFIG, pa_pin | (power -2));
            return;
        }
        else{
            power = power >= OP17 ? OP17 : ( power <= OP2 ? OP2 : power);
            lora_reg_write_byte(spid, REG_PA_DAC, 0x84);//default val to +17dBm
            lora_reg_write_byte(spid, REG_PA_CONFIG, pa_pin | (power -2));
            return;
        }
    }
}

void lora_set_dio_rx_mapping(int spid){
    lora_reg_write_byte(spid, REG_DIO_MAPPING_1, 0<<6);
}

void lora_set_dio_tx_mapping(int spid){
    lora_reg_write_byte(spid, REG_DIO_MAPPING_1, 1<<6);
}

void lora_set_rxdone_dioISR(int gpio_n, rxDoneISR func, LoRa_ctl *modem){
    gpioSetMode(gpio_n, PI_INPUT);
    gpioSetISRFuncEx(gpio_n, RISING_EDGE, 0, func, (void *)modem);
}

void lora_set_txdone_dioISR(int gpio_n, txDoneISR func, LoRa_ctl *modem){
    gpioSetMode(gpio_n, PI_INPUT);
    gpioSetISRFuncEx(gpio_n, RISING_EDGE, 0, func, (void *)modem);
}

void lora_remove_dioISR(int gpio_n){
    gpioSetISRFunc(gpio_n, RISING_EDGE, 0, NULL);
}

void LoRa_send(LoRa_ctl *modem){
    if(lora_get_op_mode(modem->spid) != STDBY_MODE){
        lora_set_satandby_mode(modem->spid);
    }
    lora_calculate_packet_t(modem);
    if(modem->eth.lowDataRateOptimize){
        lora_set_lowdatarateoptimize_on(modem->spid);
    }
    else{
        lora_set_lowdatarateoptimize_off(modem->spid);
    }
    
    if(modem->eth.implicitHeader){
        lora_write_fifo(modem->spid, modem->tx.data.buf, modem->eth.payloadLen);
    }
    else{
        lora_write_fifo(modem->spid, modem->tx.data.buf, modem->tx.data.size);
    }
    
    lora_set_dio_tx_mapping(modem->spid);
    lora_set_txdone_dioISR(modem->eth.dio0GpioN, txDoneISRf, modem);
    lora_set_tx_mode(modem->spid);
}

void LoRa_receive(LoRa_ctl *modem){
    
    lora_calculate_packet_t(modem);
    if(modem->eth.lowDataRateOptimize){
        lora_set_lowdatarateoptimize_on(modem->spid);
    }
    else{
        lora_set_lowdatarateoptimize_off(modem->spid);
    }
    
    if(lora_get_op_mode(modem->spid) != STDBY_MODE){
        lora_set_satandby_mode(modem->spid);
    }
    lora_set_dio_rx_mapping(modem->spid);
    lora_set_rxdone_dioISR(modem->eth.dio0GpioN, rxDoneISRf, modem);
    lora_set_rxcont_mode(modem->spid);
}

void lora_set_lowdatarateoptimize_on(int spid){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_3, (lora_reg_read_byte(spid, REG_MODEM_CONFIG_3) & 0xf7) | (0x01<<3));
}

void lora_set_lowdatarateoptimize_off(int spid){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_3, (lora_reg_read_byte(spid, REG_MODEM_CONFIG_3) & 0xf7));
}

void lora_get_rssi_pkt(LoRa_ctl *modem){
    modem->rx.data.RSSI = lora_reg_read_byte(modem->spid, REG_PKT_RSSI_VALUE) - (modem->eth.freq < 779E6 ? 164 : 157);
}

void lora_get_rssi_cur(LoRa_ctl *modem){
    modem->eth.curRSSI = lora_reg_read_byte(modem->spid, REG_RSSI_VALUE) - (modem->eth.freq < 779E6 ? 164 : 157);
}

void lora_get_snr(LoRa_ctl *modem){
    modem->rx.data.SNR = ((char)lora_reg_read_byte(modem->spid, REG_PKT_SNR_VALUE))*0.25;
}

void rxDoneISRf(int gpio_n, int level, uint32_t tick, void *modemptr){
    LoRa_ctl *modem = (LoRa_ctl *)modemptr;
    if(lora_reg_read_byte(modem->spid, REG_IRQ_FLAGS) & IRQ_RXDONE){
        lora_reg_write_byte(modem->spid, REG_FIFO_ADDR_PTR, lora_reg_read_byte(modem->spid, REG_FIFO_RX_CURRENT_ADDR));
        
        if(modem->eth.implicitHeader){
            lora_reg_read_bytes(modem->spid, REG_FIFO, modem->rx.data.buf, modem->eth.payloadLen);
        }
        else{
            lora_reg_read_bytes(modem->spid, REG_FIFO, modem->rx.data.buf, lora_reg_read_byte(modem->spid, REG_RX_NB_BYTES));
        }
        modem->rx.data.CRC = (lora_reg_read_byte(modem->spid, REG_IRQ_FLAGS) & 0x20);
        lora_get_rssi_pkt(modem);
        lora_get_snr(modem);
        lora_reset_irq_flags(modem->spid);
        modem->rx.callback(&modem->rx.data);
    }
}

void txDoneISRf(int gpio_n, int level, uint32_t tick, void *modemptr){
    LoRa_ctl *modem = (LoRa_ctl *)modemptr;
    if(lora_reg_read_byte(modem->spid, REG_IRQ_FLAGS) & IRQ_TXDONE){
        gettimeofday(&modem->tx.data.last_time, NULL);
        //lora_remove_dioISR(gpio_n);
        lora_reset_irq_flags(modem->spid);
        modem->tx.callback(&modem->tx.data);
        lora_set_sleep_mode(modem->spid);
    }
}

int LoRa_end(LoRa_ctl *modem){
    LoRa_stop_receive(modem);
    return spiClose(modem->spid);
}

void LoRa_stop_receive(LoRa_ctl *modem){
    lora_remove_dioISR(modem->eth.dio0GpioN);
    lora_set_sleep_mode(modem->spid);
}

unsigned char lora_get_op_mode(int spid){
    return (lora_reg_read_byte(spid, REG_OP_MODE) & 0x07);
}

void lora_calculate_packet_t(LoRa_ctl *modem){
    unsigned BW_VAL[10] = {7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000};
    
    double Tsym, Tpreamle, Tpayload, Tpacket;
    unsigned payloadSymbNb;
    
    unsigned bw = BW_VAL[(modem->eth.bw>>4)];
    unsigned sf = modem->eth.sf>>4;
    unsigned char ecr = 4+(modem->eth.ecr/2);
    unsigned char payload;
    if(modem->eth.implicitHeader){
        payload = modem->eth.payloadLen;
    }
    else{
        payload = modem->tx.data.size;
    }
    
    Tsym = (pow(2, sf)/bw)*1000;
    Tpreamle = (modem->eth.preambleLen+4.25)*Tsym;
    payloadSymbNb = 8+ceil((double)(8*payload - 4*sf + 28+16)/(4*(sf - 2*modem->eth.lowDataRateOptimize)))*ecr;
    Tpayload = payloadSymbNb*Tsym;
    Tpacket = Tpayload+Tpreamle;
    
    modem->eth.lowDataRateOptimize = (Tsym > 16);
    modem->tx.data.Tsym = Tsym;
    modem->tx.data.Tpkt = Tpacket;
    modem->tx.data.payloadSymbNb = payloadSymbNb;
}

void lora_set_addr_ptr(int spid, unsigned char addr){
    lora_reg_write_byte(spid, REG_FIFO_ADDR_PTR, addr);
}

unsigned char lora_write_fifo(int spid, char *buf, unsigned char size){
    lora_set_addr_ptr(spid, TX_BASE_ADDR);
    lora_set_payload(spid, size);
    return lora_reg_write_bytes(spid, REG_FIFO, buf, size);
}

void lora_set_satandby_mode(int spid){
    lora_reg_write_byte(spid, REG_OP_MODE, (lora_reg_read_byte(spid, REG_OP_MODE) & 0xf8) | STDBY_MODE );
}

void lora_set_rxcont_mode(int spid){
    lora_reg_write_byte(spid, REG_OP_MODE, (lora_reg_read_byte(spid, REG_OP_MODE) & 0xf8) | RXCONT_MODE );
}

void lora_set_tx_mode(int spid){
    lora_reg_write_byte(spid, REG_OP_MODE, (lora_reg_read_byte(spid, REG_OP_MODE) & 0xf8) | TX_MODE );
}

void lora_set_syncw(int spid, unsigned char word){
    lora_reg_write_byte(spid, REG_SYNC_WORD, word);
}

void lora_set_sleep_mode(int spid){
    lora_reg_write_byte(spid, REG_OP_MODE, (lora_reg_read_byte(spid, REG_OP_MODE) & 0xf8) | SLEEP_MODE );
}

void lora_set_lora_mode(int spid){
    lora_set_sleep_mode(spid);
    lora_reg_write_byte(spid, REG_OP_MODE, (lora_reg_read_byte(spid, REG_OP_MODE) & 0x7f) | LORA_MODE );
}

void lora_set_sf(int spid, SpreadingFactor sf){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_2, (lora_reg_read_byte(spid, REG_MODEM_CONFIG_2)& 0x0f) | sf );
}

void lora_set_crc_on(int spid){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_2, (lora_reg_read_byte(spid, REG_MODEM_CONFIG_2)& 0xfb) | 0x01<<2 );
}

void lora_set_crc_off(int spid){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_2, (lora_reg_read_byte(spid, REG_MODEM_CONFIG_2)& 0xfb));
}

void lora_set_bandwidth(int spid, BandWidth bw){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_1, (lora_reg_read_byte(spid, REG_MODEM_CONFIG_1)& 0x0f) | bw );
}

void lora_set_errorcr(int spid, ErrorCodingRate cr){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_1, (lora_reg_read_byte(spid, REG_MODEM_CONFIG_1)& 0xf1) | cr );
}

void lora_set_explicit_header(int spid){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_1, lora_reg_read_byte(spid, REG_MODEM_CONFIG_1) & 0xfe );
}

void lora_set_implicit_header(int spid){
    lora_reg_write_byte(spid, REG_MODEM_CONFIG_1, lora_reg_read_byte(spid, REG_MODEM_CONFIG_1) | 0x01 );
}

void lora_set_freq(int spid, double freq){
    int frf, frf_revers=0;
    frf = (int)ceil((freq/(32000000.0))*524288);
    frf_revers += (int)((unsigned char)(frf>>0))<<16;
    frf_revers += (int)((unsigned char)(frf>>8))<<8;
    frf_revers += (int)((unsigned char)(frf>>16)<<0);
    lora_reg_write_bytes(spid, REG_FR_MSB, (char *)&frf_revers, 3);
}

_Bool lora_check_conn(LoRa_ctl *modem){
    return (modem->eth.syncWord == lora_reg_read_byte(modem->spid, REG_SYNC_WORD));
}

void lora_set_preamble(LoRa_ctl* modem)
{
    if(modem->eth.preambleLen < 6){
        modem->eth.preambleLen = 6;
    }
    else if(modem->eth.preambleLen > 65535){
        modem->eth.preambleLen = 65535;
    }
    unsigned len_revers=0;
    len_revers += ((unsigned char)(modem->eth.preambleLen>>0))<<8;
    len_revers += ((unsigned char)(modem->eth.preambleLen>>8))<<0;
    lora_reg_write_bytes(modem->spid, REG_PREAMBLE_MSB, (char *)&len_revers, 2);
}

void lora_set_payload(int spid, unsigned char payload){
    lora_reg_write_byte(spid, REG_PAYLOAD_LENGTH, payload);
}

void lora_reset(unsigned char gpio_n){
    gpioSetMode(gpio_n, PI_OUTPUT);
    gpioWrite(gpio_n, 0);
    usleep(100);
    gpioWrite(gpio_n, 1);
    usleep(5000);
}

void lora_reset_irq_flags(int spid){
    lora_reg_write_byte(spid, REG_IRQ_FLAGS, 0xff);
}

unsigned char lora_reg_read_byte(int spid, unsigned char reg){
    int ret;
    char rx[2], tx[2];
    tx[0]=reg;
    tx[1]=0x00;
    
    rx[0]=0x00;
    rx[1]=0x00;
    
    ret = spiXfer(spid, tx, rx, 2);
    if(ret<0)
        return ret;
    
    if(ret<=1)
        return -1;
    
    return rx[1];
}

int lora_reg_write_byte(int spid, unsigned char reg, unsigned char byte){
    char rx[2], tx[2];
    tx[0]=(reg | 0x80);
    tx[1]=byte;
    
    rx[0]=0x00;
    rx[1]=0x00;
    
    return spiXfer(spid, tx, rx, 2);
}

int lora_reg_read_bytes(int spid, unsigned char reg, char *buff, unsigned char size){
    int ret;
    char tx[257];
    char rx[257];
    
    memset(tx, '\0', 257);
    memset(rx, '\0', 257);
    memset(buff, '\0', size);
    tx[0]=reg;
    ret = spiXfer(spid, tx, rx, size+1);
    memcpy(buff, &rx[1], ret-1);
    return ret;
}

int lora_reg_write_bytes(int spid, unsigned char reg, char *buff, unsigned char size){
    char tx[257];
    char rx[257];
    memset(tx, '\0', 257);
    memset(rx, '\0', 257);
    
    tx[0]=(reg | 0x80);
    memcpy(&tx[1], buff, size);
    return spiXfer(spid, tx, rx, size+1);
}
