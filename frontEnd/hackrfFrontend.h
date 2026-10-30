#ifndef NGENE_HACKRF_FRONTEND_H
#define NGENE_HACKRF_FRONTEND_H
#include "frontend.h"

int OpenHackrfFrontend(TNGeneConfiguration *cfg, TRxFe *fe);

int ConfigureHackrfFrontend(TNGeneConfiguration *cfg);

int StartHackrfFrontend(TRxFe *fe);

int StopHackrfFrontend(TRxFe *fe);

void CloseHackrfFrontend(TRxFe *fe);

unsigned int ReadHackrfFrontend(TRxFe *fe, char *buffer, unsigned int bytes);

int CheckHackrfFrontend(TRxFe *fe);

void ServiceHackrfFrontend(TRxFe *fe);
#endif
