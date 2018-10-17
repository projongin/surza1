
#include <string.h>

#include "common.h"

#include "delta_hmi.h"
#include "modbus_rtu.h"


#include "Rtcom.h"


//#define N_OF_WR_REGS 200
//#define WR_START_REG 100

#define N_OF_RD_REGS 1
#define RD_START_REG 180


volatile uint16_t HMI_input_regs[10];

uint16_t test_param;
uint16_t cnt_ok = 0;
uint16_t cnt_err = 0;

delta_set_regs_callback set_reg_callback;

//вызывается из прерывания, обновляет данные для отправки
void delta_hmi_write(const uint16_t* ptr) {
	/*	long cur_buf;

	if(buf_num<0){
	buf_num=0;
	cur_buf=0;
	}else
	cur_buf = buf_num?0:1;

	for(int i=0; i<N_OF_WR_REGS; i++)
	send_buf[cur_buf][i]=*ptr++;
	*/
}

//заполняет буфер на отправку непосредственно перед отправкой
bool delta_hmi_wr(uint16_t* ptr, uint16_t* start_reg, uint16_t* num) {
	/*
	if(buf_num<0)
	return false;

	for(int i=0; i<num; i++)
	*ptr++ = send_buf[buf_num][i];


	InterlockedExchange(&buf_num, buf_num?0:1);
	*/

	set_reg_callback(ptr, start_reg, num);

	return true;
}


void delta_hmi_rd(uint16_t* ptr, uint16_t num) {
	if (num) {
		//test_param = *ptr;
		global_spinlock_lock();
		HMI_input_regs[0] = *ptr;
		global_spinlock_unlock();
	}
}


static bool hmi_open_flag = false;

void delta_hmi_open(delta_set_regs_callback callback) {

	hmi_open_flag = false;

	set_reg_callback = callback;

	Modbus_Init();

	//delta->Open( COM2, 9600, PARITY_NONE, 1, 3, 0x2F8);
	//delta->Open( COM3, 9600, PARITY_NONE, 1, 15, 0x3E0);
	Modbus_Open(COM4, 9600, PARITY_NONE, 1, 15, 0x2E8);

	memset((void*)HMI_input_regs, 0, sizeof(HMI_input_regs));

	hmi_open_flag = true;

	return;
}


uint16_t read_buf[N_OF_RD_REGS];
uint16_t write_buf[200];

modbus_message_t msg_wr, msg_rd, msg;

void delta_hmi_update() {
	static uint8_t wr_label = 123;
	static uint8_t rd_label = 234;

	static bool wr_flag = false;
	static bool rd_flag = false;

	static uint16_t wr_num_regs;

	if (!hmi_open_flag)
		return;


	while (RTKGetCond(Modbus_recv_msgs, &msg)) {

		if (msg.status != STATUS_FAULT
			&& msg.msg_type == MSG_TYPE_READ_REGS
			&& msg.Count == N_OF_RD_REGS
			&& msg.label == rd_label) {
			cnt_ok++;
			rd_label++;
			delta_hmi_rd((uint16_t*)msg.data_buf, msg.Count);
			rd_flag = false;
			//Log("DELTA READ OK!");
		}
		else if (msg.status != STATUS_FAULT
			&& msg.msg_type == MSG_TYPE_WRITE_REGS
			&& msg.Count == wr_num_regs
			&& msg.label == wr_label) {
			cnt_ok++;
			wr_label++;
			wr_flag = false;
			//Log("DELTA WRITE OK!");
		}
		else {
			cnt_err++;
			// Log("DELTA FAULT!");
			if (msg.msg_type == MSG_TYPE_READ_REGS
				&& msg.label == rd_label)
				rd_flag = false;
			if (msg.msg_type == MSG_TYPE_WRITE_REGS
				&& msg.label == wr_label)
				wr_flag = false;
		}

	}


	if (!wr_flag) {
		msg_wr.adr = 1;
		msg_wr.data_buf = write_buf;
		msg_wr.label = wr_label;
		msg_wr.msg_type = MSG_TYPE_WRITE_REGS;
		//msg_wr.Start = WR_START_REG;
		//msg_wr.Count = N_OF_WR_REGS;
		if (delta_hmi_wr((uint16_t*)msg_wr.data_buf, &msg_wr.Start, &msg_wr.Count)) {
			wr_num_regs = msg_wr.Count;
			if (Modbus_message((const struct modbus_message_t*)&msg_wr)) {
				wr_flag = true;
			}
		}
	}

	if (!rd_flag) {

		//чтения с панели пока не нужно
#if 0
		msg_rd.adr = 1;
		msg_rd.data_buf = read_buf;
		msg_rd.label = rd_label;
		msg_rd.msg_type = MSG_TYPE_READ_REGS;
		msg_rd.Start = RD_START_REG;
		msg_rd.Count = N_OF_RD_REGS;
		if (Modbus_message(&msg_rd))
			rd_flag = true;
#endif
	}

}



void delta_hmi_close() {
	if(Modbus_IsOpened())
		Modbus_Close();
}
