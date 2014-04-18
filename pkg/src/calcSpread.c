#include <Rdefines.h>
#include <Rinternals.h>
#include "version.h"
#include "macros.h"
#include "convert.h"
#include "zerocurve.h"
#include "cxzerocurve.h"
#include "dateconv.h"
#include "date_sup.h"
#include "busday.h"
#include "ldate.h"
#include "cdsone.h"
#include "cds.h"
#include "cerror.h"
#include "rtbrent.h"
#include "tcurve.h"

#define NEW_ARRAY1(t,n)          (t *) JpmcdsMallocSafe(sizeof(t)*(n))

typedef struct
{
    TDate           today;
    TDate           valueDate;
    TDate           benchmarkDate; 
    TDate           stepinDate;
    TDate           startDate;
    TDate           endDate;
    double          couponRate;
    TBoolean        payAccruedOnDefault;
    TDateInterval  *dateInterval;
    TStubMethod    *stubType;
    long            accrueDCC;
    long            badDayConv;
    char           *calendar;
    TCurve         *discCurve;
    double          upfrontCharge;
    double          recoveryRate;
    TBoolean        payAccruedAtStart;
} CDSONE_SPREAD_CONTEXT;


/* static function declarations */
static int cdsoneSpreadSolverFunction
(double               onespread,
 CDSONE_SPREAD_CONTEXT *context,
 double              *diff)
{
    double upfrontCharge;

    if (JpmcdsCdsoneUpfrontCharge(context->today,
				  context->valueDate,
				  context->benchmarkDate,
				  context->stepinDate,
				  context->startDate,
				  context->endDate,
				  context->couponRate,
				  context->payAccruedOnDefault,
				  context->dateInterval,
				  context->stubType,
				  context->accrueDCC,
    				  context->badDayConv,
				  context->calendar,
				  context->discCurve,
				  onespread,
				  context->recoveryRate,
				  context->payAccruedAtStart,
				  &upfrontCharge) != SUCCESS)
      return FAILURE;
    *diff = upfrontCharge - context->upfrontCharge;
    return SUCCESS;
}

/*
***************************************************************************
** Calculate conventional spread from upfront charge.
***************************************************************************
*/

SEXP calcCdsoneSpread
(// variables for the zero curve
 SEXP baseDate_input,  /* (I) Value date  for zero curve       */
 SEXP types, //"MMMMMSSSSSSSSS"

 SEXP rates, //rates[14] = {1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9, 1e-9};/* (I) Array of swap rates              */
 
 SEXP expiries, 
 SEXP mmDCC, /* (I) DCC of MM instruments            */

 SEXP fixedSwapFreq,   /* (I) Fixed leg freqency               */
 SEXP floatSwapFreq,   /* (I) Floating leg freqency            */
 SEXP fixedSwapDCC,    /* (I) DCC of fixed leg                 */
 SEXP floatSwapDCC,    /* (I) DCC of floating leg              */
 SEXP badDayConvZC, //'M'  badDayConv for zero curve
 SEXP holidays,//'None'

 SEXP todayDate_input, /*today: T (Where T = trade date)*/
 SEXP valueDate_input, /* value date: T+3 Business Days*/
 SEXP benchmarkDate_input,  /* start date of benchmark CDS for
				     ** internal clean spread bootstrapping;
				     ** accrual Begin Date  */
 SEXP startDate_input,  /* Accrual Begin Date */
 SEXP endDate_input,  /*  Maturity (Fixed) */
 SEXP stepinDate_input,  /* T + 1*/

 SEXP couponRate_input, 	/* Fixed Coupon Amt */
 SEXP payAccruedOnDefault_input, /* TRUE in new contract */

 SEXP dccCDS, //accrueDCC_input,	/* ACT/360 */
 SEXP dateInterval,		  /* Q - 3 months in new contract */
 SEXP stubType, 		/* F/S */

 SEXP badDayConv_input, 	/* (F) Following */ 
 SEXP calendar_input,	/*  None (no holiday calendar) in new contract */

 SEXP upfrontCharge_input,
 SEXP recoveryRate_input,
 SEXP payAccruedAtStart_input	/* (True/False), True: Clean Upfront supplied */
)
{
  static char routine[] = "JpmcdsCdsoneSpread";
  SEXP status;
  TDate baseDate, todayDate, benchmarkDate, startDate, endDate, stepinDate,valueDate;
  int n; 	/* for zero curve */
  TCurve *discCurve = NULL;
  
  char* pt_holidays;
  const char* badDayConvZC_char;
  
  char* pt_mmDCC;
  char* pt_fixedSwapDCC;
  char* pt_floatSwapDCC;
  char* pt_fixedSwapFreq;
  char* pt_floatSwapFreq;
  
  char* pt_calendar;
  TBoolean payAccruedOnDefault, payAccruedAtStart;
  TStubMethod *pt_stubType;
  TDateInterval *pt_dateInterval;
  double* pt_onespread;
  double couponRate, upfrontCharge, recoveryRate;
  long accrueDCC, badDayConv;
  
  pt_onespread =  malloc(sizeof(double));
  
  CDSONE_SPREAD_CONTEXT context;
  
  baseDate_input = coerceVector(baseDate_input,INTSXP);
  baseDate = JpmcdsDate((long)INTEGER(baseDate_input)[0], 
			(long)INTEGER(baseDate_input)[1], 
			(long)INTEGER(baseDate_input)[2]);
  
  todayDate_input = coerceVector(todayDate_input,INTSXP);
  todayDate = JpmcdsDate((long)INTEGER(todayDate_input)[0], 
			 (long)INTEGER(todayDate_input)[1], 
			 (long)INTEGER(todayDate_input)[2]);
  
  valueDate_input = coerceVector(valueDate_input,INTSXP);
  valueDate = JpmcdsDate((long)INTEGER(valueDate_input)[0], 
			 (long)INTEGER(valueDate_input)[1], 
			 (long)INTEGER(valueDate_input)[2]);
  
  benchmarkDate_input = coerceVector(benchmarkDate_input,INTSXP);
  benchmarkDate = JpmcdsDate((long)INTEGER(benchmarkDate_input)[0], 
				  (long)INTEGER(benchmarkDate_input)[1], 
				  (long)INTEGER(benchmarkDate_input)[2]);
  
  stepinDate_input = coerceVector(stepinDate_input,INTSXP);
  stepinDate = JpmcdsDate((long)INTEGER(stepinDate_input)[0], 
			  (long)INTEGER(stepinDate_input)[1], 
			  (long)INTEGER(stepinDate_input)[2]);
  
  startDate_input = coerceVector(startDate_input,INTSXP);
  startDate = JpmcdsDate((long)INTEGER(startDate_input)[0], 
			 (long)INTEGER(startDate_input)[1], 
			 (long)INTEGER(startDate_input)[2]);
  
  endDate_input = coerceVector(endDate_input,INTSXP);
  endDate = JpmcdsDate((long)INTEGER(endDate_input)[0], 
		       (long)INTEGER(endDate_input)[1], 
		       (long)INTEGER(endDate_input)[2]);
  
  char* pt_types;
  types = coerceVector(types, STRSXP);
  pt_types =  (char *) CHAR(STRING_ELT(types, 0));
  holidays = coerceVector(holidays, STRSXP);
  pt_holidays =  (char *) CHAR(STRING_ELT(holidays, 0));
  
  n = strlen(CHAR(STRING_ELT(types, 0))); // for zerocurve
  rates = coerceVector(rates,REALSXP);
  
  mmDCC = coerceVector(mmDCC, STRSXP);
  pt_mmDCC = (char *) CHAR(STRING_ELT(mmDCC,0));
  
  fixedSwapFreq = coerceVector(fixedSwapFreq, STRSXP);
  pt_fixedSwapFreq = (char *) CHAR(STRING_ELT(fixedSwapFreq,0));
  
  floatSwapFreq = coerceVector(floatSwapFreq, STRSXP);
  pt_floatSwapFreq = (char *) CHAR(STRING_ELT(floatSwapFreq,0));
  
  fixedSwapDCC = coerceVector(fixedSwapDCC, STRSXP);
  pt_fixedSwapDCC = (char *) CHAR(STRING_ELT(fixedSwapDCC,0));
  
  floatSwapDCC = coerceVector(floatSwapDCC, STRSXP);
  pt_floatSwapDCC = (char *) CHAR(STRING_ELT(floatSwapDCC,0));

  
  /* fixedSwapFreq = coerceVector(fixedSwapFreq,REALSXP); */
  /* floatSwapFreq = coerceVector(floatSwapFreq,REALSXP); */
  /* fixedSwapDCC = coerceVector(fixedSwapDCC,REALSXP); */
  /* floatSwapDCC = coerceVector(floatSwapDCC,REALSXP); */
  
  /* badDayConvZC = AS_CHARACTER(badDayConvZC); */
  /* pt_badDayConvZC = CHAR(asChar(STRING_ELT(badDayConvZC, 0))); */
  badDayConvZC = AS_CHARACTER(badDayConvZC);
  badDayConvZC_char = CHAR(asChar(STRING_ELT(badDayConvZC, 0)));
  
  
  // main.c dates
  /* TDateInterval ivl; */
  /* long          dcc; */
  /* double        freq; */
  TDateInterval fixedSwapIvl_curve;
  TDateInterval floatSwapIvl_curve;
  
  long          fixedSwapDCC_curve;
  long          floatSwapDCC_curve;
    
  double        fixedSwapFreq_curve;
  double        floatSwapFreq_curve;
  
  long mmDCC_zc_main;
  static char  *routine_zc_main = "BuildExampleZeroCurve";
  
  /* if (JpmcdsStringToDayCountConv("Act/360", &mmDCC_zc_main) != SUCCESS) */
  /*   goto done; */
  
  if (JpmcdsStringToDayCountConv(pt_mmDCC, &mmDCC_zc_main) != SUCCESS)
    goto done;
  
  /* if (JpmcdsStringToDayCountConv("30/360", &dcc) != SUCCESS) */
  /*   goto done; */
  if (JpmcdsStringToDayCountConv(pt_fixedSwapDCC, &fixedSwapDCC_curve) != SUCCESS)
    goto done;
  if (JpmcdsStringToDayCountConv(pt_floatSwapDCC, &floatSwapDCC_curve) != SUCCESS)
    goto done;
  
  /* if (JpmcdsStringToDateInterval("6M", routine_zc_main, &ivl) != SUCCESS) */
  /*   goto done; */
  
  if (JpmcdsStringToDateInterval(pt_fixedSwapFreq, routine_zc_main, &fixedSwapIvl_curve) != SUCCESS)
    goto done;
  if (JpmcdsStringToDateInterval(pt_floatSwapFreq, routine_zc_main, &floatSwapIvl_curve) != SUCCESS)
    goto done;
  
  /* if (JpmcdsDateIntervalToFreq(&ivl, &freq) != SUCCESS) */
  /*   goto done; */
  
  if (JpmcdsDateIntervalToFreq(&fixedSwapIvl_curve, &fixedSwapFreq_curve) != SUCCESS)
    goto done;
  if (JpmcdsDateIntervalToFreq(&floatSwapIvl_curve, &floatSwapFreq_curve) != SUCCESS)
    goto done;
  
  expiries = coerceVector(expiries, VECSXP);


  TDate *dates_main = NULL;
  dates_main = NEW_ARRAY1(TDate, n);
  int i;
  for (i = 0; i < n; i++)
    {
      TDateInterval tmp;
      
      // if (JpmcdsStringToDateInterval(expiries[i], routine_zc_main, &tmp) != SUCCESS)
      if (JpmcdsStringToDateInterval(CHAR(asChar(VECTOR_ELT(expiries, i))), routine_zc_main, &tmp) != SUCCESS)	{
	  JpmcdsErrMsg ("%s: invalid interval for element[%d].\n", routine_zc_main, i);
	  goto done;
	}
      
      if (JpmcdsDateFwdThenAdjust(baseDate, &tmp, JPMCDS_BAD_DAY_NONE, "None", dates_main+i) != SUCCESS)
	{
	  JpmcdsErrMsg ("%s: invalid interval for element[%d].\n", routine_zc_main, i);
	  goto done;
	}
      }
  
  
  // discCurve = (TCurve*)malloc(sizeof(TCurve));
  discCurve = JpmcdsBuildIRZeroCurve(baseDate,
				     pt_types,
				     dates_main,
				     REAL(rates),
				     (long)n,
				     (long) mmDCC_zc_main,
				     (long) fixedSwapFreq_curve,
				     (long) floatSwapFreq_curve,
				     fixedSwapDCC_curve,
				     floatSwapDCC_curve,
				     (char) *badDayConvZC_char,
				     pt_holidays);

  
  if (discCurve == NULL) JpmcdsErrMsg("IR curve not available ... \n");
  
  couponRate = *REAL(couponRate_input);
  
  payAccruedOnDefault = *LOGICAL(payAccruedOnDefault_input);
  pt_dateInterval = (TDateInterval*)malloc(sizeof(TDateInterval));
  pt_dateInterval->prd_typ = *CHAR(STRING_ELT(coerceVector(dateInterval, STRSXP), 0));
  pt_dateInterval->prd = 6;
  pt_dateInterval->flag = 0;
  
  TBoolean stubAtEnd = FALSE;
  TBoolean longStub = FALSE;
  pt_stubType = (TStubMethod*)malloc(sizeof(TStubMethod));
  pt_stubType->stubAtEnd = stubAtEnd;
  pt_stubType->longStub = longStub;
    
  /* accrueDCC = *INTEGER(dccCDS); */
  /* badDayConv = *INTEGER(badDayConv_input); */

  char* pt_dccCDS;
  dccCDS = coerceVector(dccCDS, STRSXP);
  pt_dccCDS = (char *) CHAR(STRING_ELT(dccCDS,0));

  char* pt_badDayConv;
  badDayConv_input = coerceVector(badDayConv_input, STRSXP);
  pt_badDayConv = (char *) CHAR(STRING_ELT(badDayConv_input,0));

  if (JpmcdsStringToDayCountConv(pt_dccCDS, &accrueDCC) != SUCCESS)
    goto done;


  pt_calendar = (char *) CHAR(STRING_ELT(coerceVector(calendar_input, STRSXP), 0));
  upfrontCharge = *REAL(upfrontCharge_input);
  recoveryRate = *REAL(recoveryRate_input);
  payAccruedAtStart = *LOGICAL(payAccruedAtStart_input);
    
  context.today               = todayDate;
  context.valueDate           = valueDate;
  context.benchmarkDate       = benchmarkDate;
  context.stepinDate          = stepinDate;
  context.startDate           = startDate;
  context.endDate             = endDate;
  context.couponRate          = couponRate;
  context.payAccruedOnDefault = payAccruedOnDefault;
  context.dateInterval        = pt_dateInterval;
  context.stubType            = pt_stubType;
  context.accrueDCC           = (long) accrueDCC;
  // context.badDayConv          = (long) badDayConv;
  context.badDayConv          = *pt_badDayConv;
  context.calendar            = pt_calendar;
  context.discCurve           = discCurve;
  context.upfrontCharge       = upfrontCharge;
  context.recoveryRate        = recoveryRate;
  context.payAccruedAtStart   = payAccruedAtStart;
     
  if (JpmcdsRootFindBrent ((TObjectFunc)cdsoneSpreadSolverFunction,
			   &context,
			   0.0,    /* boundLo */
			   100.0,  /* boundHi */
			   100,    /* numIterations */
			   0.01,   /* guess */
			   0.0001, /* initialXStep */
			   0.0,    /* initialFDeriv */
			   1e-8,   /* xacc */
			   1e-8,   /* facc */
			   pt_onespread) != SUCCESS)
    goto done;      

 done:

  PROTECT(status = allocVector(REALSXP, 1));
  REAL(status)[0] = (*pt_onespread) * 1e4;
  UNPROTECT(1);

  //    if (status != SUCCESS)
  //    JpmcdsErrMsgFailure (routine);
  return status;
}