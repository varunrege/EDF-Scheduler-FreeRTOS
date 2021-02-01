/*
    ESFree V1.0 - Copyright (C) 2016 Robin Kase
    All rights reserved

    This file is part of ESFree.

    ESFree is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public licence (version 2) as published by the
    Free Software Foundation AND MODIFIED BY one exception.

    ***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes ESFree without being     !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of ESFree.                                                !<<
    ***************************************************************************

    ESFree is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. Full license text can be found on license.txt.
*/

#include "scheduler.h"

#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	#include "list.h"
#endif /* schedSCHEDULING_POLICY_EDF */

#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	#define schedUSE_TCB_SORTED_LIST 1	
#endif /* schedSCHEDULING_POLICY_EDF */
#define schedTHREAD_LOCAL_STORAGE_POINTER_INDEX 0


/* Extended Task control block for managing periodic tasks within this library. */
typedef struct xExtended_TCB
{
	TaskFunction_t pvTaskCode; 		/* Function pointer to the code that will be run periodically. */
	const char *pcName; 			/* Name of the task. */
	UBaseType_t uxStackDepth; 			/* Stack size of the task. */
	void *pvParameters; 			/* Parameters to the task function. */
	UBaseType_t uxPriority; 		/* Priority of the task. */
	TaskHandle_t *pxTaskHandle;		/* Task handle for the task. */
	TickType_t xReleaseTime;		/* Release time of the task. */
	TickType_t xRelativeDeadline;	/* Relative deadline of the task. */
	TickType_t xAbsoluteDeadline;	/* Absolute deadline of the task. */
	TickType_t xPeriod;				/* Task period. */
	TickType_t xLastWakeTime; 		/* Last time stamp when the task was running. */
	TickType_t xMaxExecTime;		/* Worst-case execution time of the task. */
	TickType_t xExecTime;			/* Current execution time of the task. */

	BaseType_t xWorkIsDone; 		/* pdFALSE if the job is not finished, pdTRUE if the job is finished. */

	#if( schedUSE_TCB_SORTED_LIST == 1 )
		ListItem_t xTCBListItem; 	/* Used to reference TCB from the TCB list. */		
	#endif /* schedUSE_TCB_SORTED_LIST */

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		BaseType_t xExecutedOnce;	/* pdTRUE if the task has executed once. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 || schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		TickType_t xAbsoluteUnblockTime; /* The task will be unblocked at this time if it is blocked by the scheduler task. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME || schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		BaseType_t xSuspended; 		/* pdTRUE if the task is suspended. */
		BaseType_t xMaxExecTimeExceeded; /* pdTRUE when execTime exceeds maxExecTime. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */ 	
} SchedTCB_t;

#if( schedUSE_TCB_SORTED_LIST == 1 )
	static void prvAddTCBToList( SchedTCB_t *pxTCB );
	static void prvDeleteTCBFromList(  SchedTCB_t *pxTCB );
#endif /* schedUSE_TCB_ARRAY */

static TickType_t xSystemStartTime = 0;

static void prvPeriodicTaskCode( void *pvParameters );
static void prvCreateAllTasks( void );


#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	
	static void prvInitEDF( void );
	static void prvUpdatePrioritiesEDF( void );		
 
	#if( schedUSE_TCB_SORTED_LIST == 1 )
		static void prvSwapList( List_t **ppxList1, List_t **ppxList2 );
	#endif /* schedUSE_TCB_SORTED_LIST */
	
#endif /* schedSCHEDULING_POLICY_EDF */

#if( schedUSE_SCHEDULER_TASK == 1 )
	
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB );
	static void prvSchedulerFunction( void );
	static void prvCreateSchedulerTask( void );
	static void prvWakeScheduler( void );

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB );
		static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount );
		static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount );		
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask );
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_TCB_SORTED_LIST == 1 )
	
	static List_t xTCBList;				/* Sorted linked list for all periodic tasks. */
	static List_t xTCBTempList;			/* A temporary list used for switching lists. */
	static List_t xTCBOverflowedList; 	/* Sorted linked list for periodic tasks that have overflowed deadline. */
	static List_t *pxTCBList = NULL;  			/* Pointer to xTCBList. */
	static List_t *pxTCBTempList = NULL;		/* Pointer to xTCBTempList. */
	static List_t *pxTCBOverflowedList = NULL;	/* Pointer to xTCBOverflowedList. */
	
#endif /* schedUSE_TCB_ARRAY */

#if( schedUSE_SCHEDULER_TASK )
	static TickType_t xSchedulerWakeCounter = 0;
	static TaskHandle_t xSchedulerHandle = NULL;
#endif /* schedUSE_SCHEDULER_TASK */





#if( schedUSE_TCB_SORTED_LIST == 1 )
	/* Add an extended TCB to sorted linked list. */
	static void prvAddTCBToList( SchedTCB_t *pxTCB )
	{
		/* Initialise TCB list item. */
		vListInitialiseItem( &pxTCB->xTCBListItem );
		/* Set owner of list item to the TCB. */
		listSET_LIST_ITEM_OWNER( &pxTCB->xTCBListItem, pxTCB );
		/* List is sorted by absolute deadline value. */
		listSET_LIST_ITEM_VALUE( &pxTCB->xTCBListItem, pxTCB->xAbsoluteDeadline );		

		/* Insert TCB into list. */
		vListInsert( pxTCBList, &pxTCB->xTCBListItem );				
	}
	
	/* Delete an extended TCB from sorted linked list. */
	static void prvDeleteTCBFromList(  SchedTCB_t *pxTCB )
	{
		uxListRemove( /* your implementation goes here */ &pxTCB->xTCBListItem);
		vPortFree( pxTCB );
	}
#endif /* schedUSE_TCB_SORTED_LIST */


#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	#if( schedUSE_TCB_SORTED_LIST == 1 )
		/* Swap content of two lists. */
		static void prvSwapList( List_t **ppxList1, List_t **ppxList2 )
		{
			/* your implementation goes here */
			List_t *pxTemp;
			pxTemp = *ppxList1;
			*ppxList1 = *ppxList2;
			*ppxList2 = pxTemp;
		}
	#endif /* schedUSE_TCB_SORTED_LIST */

	/* Update priorities of all periodic tasks with respect to EDF policy. */
	static void prvUpdatePrioritiesEDF( void )
	{
	  SchedTCB_t *pxTCB;

		#if( schedUSE_TCB_SORTED_LIST == 1 )
			ListItem_t *pxTCBListItem;
			ListItem_t *pxTCBListItemTemp;
		
			if( listLIST_IS_EMPTY( pxTCBList ) && !listLIST_IS_EMPTY( pxTCBOverflowedList ) )
			{
				prvSwapList( &pxTCBList, &pxTCBOverflowedList );
			}

			const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
			pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );

			while( pxTCBListItem != pxTCBListEndMarker )
			{
				pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );

				/* Update priority in the SchedTCB list. */
				/* your implementation goes here. */
				listSET_LIST_ITEM_VALUE( pxTCBListItem, pxTCB->xAbsoluteDeadline );


				pxTCBListItemTemp = pxTCBListItem;
				pxTCBListItem = listGET_NEXT( pxTCBListItem );
				uxListRemove( pxTCBListItem->pxPrevious );

				/* If absolute deadline overflowed, insert TCB to overflowed list. */
				/* your implementation goes here. */
				if( pxTCB->xAbsoluteDeadline < pxTCB->xLastWakeTime )
				{
					vListInsert( pxTCBOverflowedList, pxTCBListItemTemp );
				}
				
				/* else Insert TCB into temp list in usual case. */				
				else /* Insert TCB into temp list in usual case. */
				{
					vListInsert( pxTCBTempList, pxTCBListItemTemp );
				}			
			}

			/* Swap list with temp list. */
			prvSwapList( &pxTCBList, &pxTCBTempList );

			#if( schedUSE_SCHEDULER_TASK == 1 )
				BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY - 1;
			#else
				BaseType_t xHighestPriority = configMAX_PRIORITIES - 1;
			#endif /* schedUSE_SCHEDULER_TASK */

			/* assign priorities to tasks */
			/* your implementation goes here. */
			const ListItem_t *pxTCBListEndMarkerAfterSwap = listGET_END_MARKER( pxTCBList );
			pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
			while( pxTCBListItem != pxTCBListEndMarkerAfterSwap )
			{
				pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );
				configASSERT( -1 <= xHighestPriority );
				pxTCB->uxPriority = xHighestPriority;
				vTaskPrioritySet( *pxTCB->pxTaskHandle, pxTCB->uxPriority );

				xHighestPriority--;
				pxTCBListItem = listGET_NEXT( pxTCBListItem );
			}		
		#endif /* schedUSE_TCB_SORTED_LIST */
	}	
#endif /* schedSCHEDULING_POLICY_EDF */


/* The whole function code that is executed by every periodic task.
 * This function wraps the task code specified by the user. */
static void prvPeriodicTaskCode( void *pvParameters )/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
{
  SchedTCB_t *pxThisTask;
  TaskHandle_t xHandle = xTaskGetCurrentTaskHandle();
  SchedTCB_t *TempTask;
  
  const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
  const ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
  
  while( pxTCBListItem != pxTCBListEndMarker )
  {
    pxThisTask = listGET_LIST_ITEM_OWNER( pxTCBListItem );       
    // your implementation goes here.
    
    if(*pxThisTask->pxTaskHandle == xHandle)
    {
      TempTask = pxThisTask;
    }
          
	  pxTCBListItem = listGET_NEXT( pxTCBListItem );
  } 

	if( 0 != TempTask->xReleaseTime )
	{
		vTaskDelayUntil( &TempTask->xLastWakeTime, TempTask->xReleaseTime );
	}
	
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		TempTask->xExecutedOnce = pdTRUE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */	

	for( ; ; )
	{
		#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
			/* Wake up the scheduler task to update priorities of all periodic tasks. */
			prvWakeScheduler();
		#endif /* schedSCHEDULING_POLICY_EDF */
		
		TempTask->xWorkIsDone = pdFALSE;

		/* Execute the task function specified by the user. */
		TempTask->pvTaskCode( pvParameters );

		TempTask->xWorkIsDone = pdTRUE;
    Serial.println(pxThisTask->pcName);
    Serial.flush();
    Serial.println(pxThisTask->xLastWakeTime);
    Serial.flush();
		TempTask->xExecTime = 0;

		#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )		
			
			/* your implementation goes here. */
			TempTask->xAbsoluteDeadline = TempTask->xLastWakeTime + TempTask->xPeriod + TempTask->xRelativeDeadline;
			
			
			/* Wake up the scheduler task to update priorities of all periodic tasks. */
			prvWakeScheduler();			
			
		#endif /* schedSCHEDULING_POLICY_EDF */

		vTaskDelayUntil( &TempTask->xLastWakeTime, TempTask->xPeriod );
	}
}

/* Creates a periodic task. */
void vSchedulerPeriodicTaskCreate( TaskFunction_t pvTaskCode, const char *pcName, UBaseType_t uxStackDepth, void *pvParameters, UBaseType_t uxPriority,
		TaskHandle_t *pxCreatedTask, TickType_t xPhaseTick, TickType_t xPeriodTick, TickType_t xMaxExecTimeTick, TickType_t xDeadlineTick )
{
	taskENTER_CRITICAL();
 
	SchedTCB_t *pxNewTCB;
  	pxNewTCB = pvPortMalloc( sizeof( SchedTCB_t ) );
	


	/* Intialize item. */
	pxNewTCB->pvTaskCode = pvTaskCode;
	pxNewTCB->pcName = pcName;
	pxNewTCB->uxStackDepth = uxStackDepth;
	pxNewTCB->pvParameters = pvParameters;
	pxNewTCB->uxPriority = uxPriority;
	pxNewTCB->pxTaskHandle = pxCreatedTask;
	pxNewTCB->xReleaseTime = xPhaseTick;
	pxNewTCB->xPeriod = xPeriodTick;
	/* populate the rest */ 
	pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
	pxNewTCB->xRelativeDeadline = xDeadlineTick; 
	pxNewTCB->xWorkIsDone = pdTRUE; 
	pxNewTCB->xExecTime = 0;	
	
	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
		pxNewTCB->xAbsoluteDeadline = pxNewTCB->xRelativeDeadline + pxNewTCB->xReleaseTime + xSystemStartTime;
		pxNewTCB->uxPriority = -1;
	#endif /* schedSCHEDULING_POLICY */
 
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		/* member initialization */
		pxNewTCB->xExecutedOnce = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
 
	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		/* member initialization */
		pxNewTCB->xSuspended = pdFALSE;
		pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	
	#if( schedUSE_TCB_SORTED_LIST == 1 )		
		prvAddTCBToList( pxNewTCB );
	#endif /* schedUSE_TCB_SORTED_LIST */
	
	taskEXIT_CRITICAL();
}

/* Deletes a periodic task. */
void vSchedulerPeriodicTaskDelete( TaskHandle_t xTaskHandle ) ////////////////////////////////////////////////////////////////////////////////////////////////////
{
	if( xTaskHandle != NULL )
	{
		#if( schedUSE_TCB_SORTED_LIST == 1 )
		
		  SchedTCB_t *pxThisTask;
		  const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
		  const ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
		  SchedTCB_t *TempTask;
     
		  while( pxTCBListItem != pxTCBListEndMarker )
		  {
			  pxThisTask = listGET_LIST_ITEM_OWNER( pxTCBListItem );
			  // your implementation goes here.
        if(*pxThisTask->pxTaskHandle == xTaskHandle)
        {
          TempTask = pxThisTask;
        }

			  pxTCBListItem = listGET_NEXT( pxTCBListItem );
		  }   

		  prvDeleteTCBFromList( TempTask );
		  //prvDeleteTCBFromList( ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX ) );
		#endif /* schedUSE_TCB_ARRAY */
	}
		
	vTaskDelete( xTaskHandle );
}

/* Creates all periodic tasks stored in TCB array, or TCB list. */
static void prvCreateAllTasks( void )
{
	SchedTCB_t *pxTCB;

	#if( schedUSE_TCB_SORTED_LIST == 1 )
	
		const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
		ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );		
	
		while( pxTCBListItem != pxTCBListEndMarker )
		{
			pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );
			configASSERT( NULL != pxTCB );
			BaseType_t xReturnValue = xTaskCreate( /* your implementation goes here. */ prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle);			
			pxTCBListItem = listGET_NEXT( pxTCBListItem );      			
		}	
	#endif /* schedUSE_TCB_SORTED_LIST */
}


#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
	/* Initializes priorities of all periodic tasks with respect to EDF policy. */
	static void prvInitEDF( void )
	{
		SchedTCB_t *pxTCB;
	
		#if( schedUSE_SCHEDULER_TASK == 1 )
			UBaseType_t uxHighestPriority = schedSCHEDULER_PRIORITY - 1;
		#else
			UBaseType_t uxHighestPriority = configMAX_PRIORITIES - 1;
		#endif /* schedUSE_SCHEDULER_TASK */

		const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
		ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );

		while( pxTCBListItem != pxTCBListEndMarker )
		{
			/* assigning priorities to sorted tasks */
			/* your implementation goes here. */		
			pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );

			pxTCB->uxPriority = uxHighestPriority;
			uxHighestPriority--;

			pxTCBListItem = listGET_NEXT( pxTCBListItem ); 
		}		
	}
#endif /* schedSCHEDULING_POLICY */


#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )

	/* Recreates a deleted task that still has its information left in the task array (or list). */
	static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB )
	{
	  BaseType_t xReturnValue = xTaskCreate( /* your implementation goes here. */ prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth, pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle );
		if( pdPASS == xReturnValue )
		{
			/* This must be set to false so that the task does not miss the deadline immediately when it is created. */
			pxTCB->xExecutedOnce = pdFALSE;
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				/* your implementation goes here. */	
				pxTCB->xSuspended = pdFALSE;
				pxTCB->xMaxExecTimeExceeded = pdFALSE;			
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
		}
		else
		{
			/* if task creation failed */
		}
	}

	/* Called when a deadline of a periodic task is missed.
	 * Deletes the periodic task that has missed it's deadline and recreate it.
	 * The periodic task is released during next period. */
	static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{
		/* Delete the pxTask and recreate it. Hint: vTaskDelete()*/
		
		/* your implementation goes here. */	
		vTaskDelete( *pxTCB->pxTaskHandle );
		pxTCB->xExecTime = 0;
		prvPeriodicTaskRecreate( pxTCB );

		pxTCB->xReleaseTime = pxTCB->xLastWakeTime + pxTCB->xPeriod;

		/* Need to reset lastWakeTime for correct release. */
		/* your implementation goes here. */	
		pxTCB->xLastWakeTime = 0;
		pxTCB->xAbsoluteDeadline = pxTCB->xRelativeDeadline + pxTCB->xReleaseTime;	
	}

	/* Checks whether given task has missed deadline or not. */
	static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{
		if( ( NULL != pxTCB ) && ( pdFALSE == pxTCB->xWorkIsDone ) && ( pdTRUE == pxTCB->xExecutedOnce ) )
		{
			/* check whether deadline is missed. */
			/* your implementation goes here. */	
			if( ( signed ) ( pxTCB->xAbsoluteDeadline - xTickCount ) < 0 )
			{
				/* If deadline is missed. */
				prvDeadlineMissedHook( pxTCB, xTickCount );
       
			}
		}
    /*Serial.println("Absolute deadline"); //Release time of the task. 
    Serial.println(pxTCB->xAbsoluteDeadline); //Absolute deadline of the task. 
    Serial.flush();
    Serial.println("Period"); //Release time of the task. 
    Serial.println(pxTCB->xPeriod); //Task period. 
    Serial.flush();
    Serial.println("WCET");  
    Serial.println(pxTCB->xMaxExecTime);// Worst-case execution time of the task. 
    Serial.flush();*/
	}

	
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */


#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )

	/* Called if a periodic task has exceeded it's worst-case execution time.
	 * The periodic task is blocked until next period. A context switch to
	 * the scheduler task occur to block the periodic task. */
	static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask )
	{
		/* your implementation goes here. */	
		pxCurrentTask->xMaxExecTimeExceeded = pdTRUE;
		/* Is not suspended yet, but will be suspended by the scheduler later. */
		pxCurrentTask->xSuspended = pdTRUE;
		pxCurrentTask->xAbsoluteUnblockTime = pxCurrentTask->xLastWakeTime + pxCurrentTask->xPeriod;
		pxCurrentTask->xExecTime = 0;
   		
		BaseType_t xHigherPriorityTaskWoken;
		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
		xTaskResumeFromISR( xHigherPriorityTaskWoken );
	}

#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Called by the scheduler task. Checks all tasks for any enabled
	 * Timing Error Detection feature. */
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB )
	{
		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
			
				/* Since lastWakeTime is updated to next wake time when the task is delayed, tickCount > lastWakeTime implies that
				 * the task has not finished it's job this period. */

				/* check if task missed deadline */
				/* your implementation goes here. */	
				if( ( signed ) ( xTickCount - pxTCB->xLastWakeTime ) > 0 )
				{
					pxTCB->xWorkIsDone = pdFALSE;
				}
				
				prvCheckDeadline( pxTCB, xTickCount );			
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
		

		
		#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		/* check if task exceeded WCET */
			/* your implementation goes here. Hint: use vTaskSuspend() */		
			if( pdTRUE == pxTCB->xMaxExecTimeExceeded )
			{
				pxTCB->xMaxExecTimeExceeded = pdFALSE;
				vTaskSuspend( *pxTCB->pxTaskHandle );
			}
			if( pdTRUE == pxTCB->xSuspended )
			{
				if( ( signed ) ( pxTCB->xAbsoluteUnblockTime - xTickCount ) <= 0 )
				{
					pxTCB->xSuspended = pdFALSE;
					pxTCB->xLastWakeTime = xTickCount;
					vTaskResume( *pxTCB->pxTaskHandle );
				}
			}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
		

		return;
	}

	/* Function code for the scheduler task. */
	static void prvSchedulerFunction( void )
	{
		for( ; ; )
		{
			#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
				prvUpdatePrioritiesEDF();													
			#endif /* schedSCHEDULING_POLICY_EDF */

			#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				TickType_t xTickCount = xTaskGetTickCount();
				SchedTCB_t *pxTCB;

				#if( schedUSE_TCB_SORTED_LIST == 1 )
					const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
					ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );					
					
					while( pxTCBListItem != pxTCBListEndMarker )
					{
						/* your implementation goes here */
						pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem);

						prvSchedulerCheckTimingError( xTickCount, pxTCB );

						pxTCBListItem = listGET_NEXT( pxTCBListItem );
					}
				#endif /* schedUSE_TCB_SORTED_LIST */
			
			#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

			ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		}
	}

	/* Creates the scheduler task. */
	static void prvCreateSchedulerTask( void )
	{
		xTaskCreate( (TaskFunction_t) prvSchedulerFunction, "Scheduler", schedSCHEDULER_TASK_STACK_SIZE, NULL, schedSCHEDULER_PRIORITY, &xSchedulerHandle );
	}
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_SCHEDULER_TASK == 1 )

	/* Wakes up (context switches to) the scheduler task. */
	static void prvWakeScheduler( void )
	{
		BaseType_t xHigherPriorityTaskWoken;
		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
		xTaskResumeFromISR( xHigherPriorityTaskWoken );
	}

	/* Called every software tick. */
	void vApplicationTickHook( void )////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	{
		SchedTCB_t *pxCurrentTask;
		TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();
    SchedTCB_t *TempTask;
		const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
		const ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
		
		while( pxTCBListItem != pxTCBListEndMarker )
		{
			// your implementation goes here
      if(*pxCurrentTask->pxTaskHandle == xCurrentTaskHandle)
      {
        TempTask = pxCurrentTask; 
      }
		}   


//    pxCurrentTask = ( SchedTCB_t * ) pvTaskGetThreadLocalStoragePointer( xCurrentTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX );
   
		if( NULL != TempTask && xCurrentTaskHandle != xSchedulerHandle && xCurrentTaskHandle != xTaskGetIdleTaskHandle() )
		{
			TempTask->xExecTime++;
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				/* your implementation goes here */
				if( TempTask->xMaxExecTime <= TempTask->xExecTime )
				{
					if( pdFALSE == TempTask->xMaxExecTimeExceeded )
					{
						if( pdFALSE == TempTask->xSuspended )
						{
							prvExecTimeExceedHook( xTaskGetTickCountFromISR(), TempTask );
						}
					}
				}
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
		}

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
			xSchedulerWakeCounter++;
			if( xSchedulerWakeCounter == schedSCHEDULER_TASK_PERIOD )
			{
				xSchedulerWakeCounter = 0;
				prvWakeScheduler();
			}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	}
#endif /* schedUSE_SCHEDULER_TASK */

/* This function must be called before any other function call from this module. */
void vSchedulerInit( void )
{
	#if( schedUSE_TCB_SORTED_LIST == 1 )
		vListInitialise( &xTCBList );
		vListInitialise( &xTCBTempList );
		vListInitialise( &xTCBOverflowedList );
		pxTCBList = &xTCBList;
		pxTCBTempList = &xTCBTempList;
		pxTCBOverflowedList = &xTCBOverflowedList;		
	#endif 
}

/* Starts scheduling tasks. All periodic tasks (including polling server) must
 * have been created with API function before calling this function. */
void vSchedulerStart( void )
{
	
	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
		prvInitEDF();
	#endif /* schedSCHEDULING_POLICY */

	#if( schedUSE_SCHEDULER_TASK == 1 )
		prvCreateSchedulerTask();
	#endif /* schedUSE_SCHEDULER_TASK */

	prvCreateAllTasks();

	xSystemStartTime = xTaskGetTickCount();
	vTaskStartScheduler();
}
