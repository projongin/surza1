#include <Rtk32.h>
#include <Rtkflt.h> 

#include <stdlib.h>

#include "ai8s.h"
#include "log.h"


//настраиваемый номер прерывания
#define AI8S_IRQ                 6


#define AI8S_CONTROL_REG         0
#define AI8S_DATA_REG            0
#define AI8S_OUTPUTS_REG         2
#define AI8S_MAXC_REG            4
#define AI8S_GAIN_REG            6
#define AI8S_FIFO_CONTROL_REG   11
#define AI8S_FIFO_DATA_REG      12
#define AI8S_DAC_REG            14
#define AI8S_ID_REG             14


#define RDY_BIT             0x0080


#pragma pack(push)
#pragma pack(1) 

typedef union {
	struct {
		uint16_t N : 1;
		uint16_t INT : 1;
		uint16_t DMA : 1;
		uint16_t DIV_10 : 1;
		uint16_t TMR : 1;
		uint16_t BANK : 1;
		uint16_t AO_RDY : 1;
		uint16_t ST_RDY : 1;
		uint16_t MUL_10 : 1;
		uint16_t _none : 2;
		uint16_t FIFO : 1;
		uint16_t NWR : 1;
		uint16_t SHARE : 1;
		uint16_t SINGLE : 1;
		uint16_t FAST : 1;
	};
	uint16_t all;
} AI8S_CONTROL_REG_t;

#pragma pack(pop)


static AI8S_CONTROL_REG_t ctrl;


logic_adc_handler logic_handler;

static void* FPUContext;


int RTKAPI AI8S_irq_handler(void* P) {

	_rtkFLTSave(FPUContext);

	if (logic_handler)
		logic_handler();
	else
		(void)ai8s_read_ch(0, 7);

	_rtkFLTRestore(FPUContext);

	return 0;
}

static unsigned ai8s_adr1;
static unsigned ai8s_adr2;


bool InitAI8S(unsigned adc_num, unsigned adc1_adr, unsigned adc2_adr, unsigned period, logic_adc_handler handler){

	ai8s_adr1 = adc1_adr;
	ai8s_adr2 = adc2_adr;

	RTOutW(adc1_adr + AI8S_CONTROL_REG, 0x0020);
	if (adc_num>1)
		RTOutW(adc2_adr + AI8S_CONTROL_REG, 0x0020);


	if (RTIn(adc1_adr + AI8S_ID_REG) != 'A') {
		LOG_AND_SCREEN("ERROR! ADC1 not found!");
		return false;
	}

    if(adc_num>1)
		if (RTIn(adc2_adr + AI8S_ID_REG) != 'A') {
			LOG_AND_SCREEN("ERROR! ADC2 not found!");
			return false;
		}


	RTOutW(adc1_adr + AI8S_CONTROL_REG, 0);
	if (adc_num>1)
		RTOutW(adc2_adr + AI8S_CONTROL_REG, 0);

	//запись периода срабатывания
	RTOutW(adc1_adr + AI8S_MAXC_REG, period);

	//очистка fifo (на всяк случай)
	RTOut(adc1_adr + AI8S_FIFO_CONTROL_REG, 0x10);
	if (adc_num>1)
		RTOut(adc2_adr + AI8S_FIFO_CONTROL_REG, 0x10);


	FPUContext = calloc(_rtkFLTDataSize(), 1);

	//настройка обработчика прерывания
	logic_handler = handler;
	RTInstallSharedIRQHandlerEx(AI8S_IRQ, AI8S_irq_handler, NULL);

	//делаю прерывание AI8S самым приоритетным
#if 0
	RTKIRQTopPriority(AI8S_IRQ, 8);
	RTKSetIRQStack(AI8S_IRQ, 65536);
#endif


	//контрольные регистры плат

	if (adc_num > 1) {
		ctrl.N = 0;
		ctrl.INT = 0;
		ctrl.DMA = 0;
		ctrl.DIV_10 = 0;
		ctrl.TMR = 0;
		ctrl.BANK = 0;
		ctrl.AO_RDY = 0;
		ctrl.ST_RDY = 0;
		ctrl.MUL_10 = 0;
		ctrl._none = 0;
		ctrl.FIFO = 0;
		ctrl.NWR = 0;
		ctrl.SHARE = 0;
		ctrl.SINGLE = 0;
		ctrl.FAST = 0;

		RTOutW(adc2_adr + AI8S_CONTROL_REG, ctrl.all);
	}
	
	ctrl.N = 0;
	ctrl.INT = 1;
	ctrl.DMA = 0;
	ctrl.DIV_10 = 0;
	ctrl.TMR = 1;
	ctrl.BANK = 0;
	ctrl.AO_RDY = 0;
	ctrl.ST_RDY = 0;
	ctrl.MUL_10 = 0;
	ctrl._none = 0;
	ctrl.FIFO = 0;
	ctrl.NWR = 0;
	ctrl.SHARE = 0;
	ctrl.SINGLE = 0;
	ctrl.FAST = 0;

	RTOutW(adc1_adr + AI8S_CONTROL_REG, ctrl.all);

	//настройка cntl на постоянный запуск измерений второго ацп
	ctrl.all = 0;

	return true;
}



//запуск второго ацп
void ai8s_start_second_adc() {
	ctrl.ST_RDY = 1;
	ctrl.BANK = 1;
	RTOutW(ai8s_adr2 + AI8S_CONTROL_REG, ctrl.all);
}

//ожидание измерений второго ацп
bool ai8s_wait_second_adc() {

	//читаю результаты второй платы
	uint8_t in;
	uint32_t cnt = 0;
	do {
		in = RTIn(ai8s_adr2 + AI8S_CONTROL_REG);
		cnt++;
	} while (!(in & 0x80) && cnt<20);    //ожидание конца измерений

	if (cnt >= 20)
		return false;

	//настройка на чтение результатов
	ctrl.ST_RDY = 0;
	ctrl.BANK = 0;
	RTOutW(ai8s_adr2 + AI8S_CONTROL_REG, ctrl.all);

	return true;
}

//чтение канала ацп
int32_t ai8s_read_ch(unsigned adc_num, unsigned ch_num) {

	uint16_t u16 = RTInW((adc_num?ai8s_adr2:ai8s_adr1) + AI8S_DATA_REG + (ch_num << 1));

	return *((int16_t*)&u16);
}

