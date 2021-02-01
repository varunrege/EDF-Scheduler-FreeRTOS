#include "scheduler.h"

TaskHandle_t xHandle1 = NULL;
TaskHandle_t xHandle2 = NULL;



// the loop function runs over and over again forever
void loop() {}


static void testFunc1( void *pvParameters )
{
	(void) pvParameters;
	int i,a;
	/*for( i = 0; i < 3; i++ )
	{
		Serial.println(i);
	}
    Serial.println("T1");*/
}

static void testFunc2( void *pvParameters )
{ 
	(void) pvParameters;	
	int i, a;	
	/*for(i = 0; i < 10; i++ )
	{
		a = i;
	}
   Serial.println("T2");*/
}

int main( void )
{
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB, on LEONARDO, MICRO, YUN, and other 32u4 based boards.
  }
	char c1 = 'a';
	char c2 = 'b';			

	vSchedulerInit();

	vSchedulerPeriodicTaskCreate(testFunc1, "t1", configMINIMAL_STACK_SIZE, &c1, 1, &xHandle1, pdMS_TO_TICKS(0), pdMS_TO_TICKS(400), pdMS_TO_TICKS(100), pdMS_TO_TICKS(400));
	vSchedulerPeriodicTaskCreate(testFunc2, "t2", configMINIMAL_STACK_SIZE, &c2, 2, &xHandle2, pdMS_TO_TICKS(0), pdMS_TO_TICKS(800), pdMS_TO_TICKS(200), pdMS_TO_TICKS(700));



	vSchedulerStart();

	/* If all is well, the scheduler will now be running, and the following line
	will never be reached. */
	
	for( ;; );
}
