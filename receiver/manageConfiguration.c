/**
 * @file manageConfiguration.c
 *
 * @author Mario Nicola
 *
 * @date 06/08/2014
 *
 * @version 1.0.0
 *
 * @brief Functions used to to read receiver configuration.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <ctype.h>
#include "ngeneConfiguration.h"
#include "manageConfiguration.h"
#include "ngeneDataStructure.h"
#include "ngeneConstants.h"
#include "timeConstants.h"
#include "ioFunctions.h"
#include "closeManager.h"
#include "managePvtConfiguration.h"
#include "manageAcqConfiguration.h"
#include "manageTrkConfiguration.h"
#include "manageWarmStartConfiguration.h"
#include "manageNavMsgConfiguration.h"
#include "manageUsbFrontEndConfiguration.h"
#include "spreadingCodes.h"
#include "rinexLog.h"
#include "nmeaLog.h"
#include "prnList.h"
#include "prnListDataStructure.h"
#include "checkFolder.h"
#include "timeConversions.h"


/* Compare the active configuration version with the bundled template version. */


/**
 * @def VERSION_INDEX_LEN
 * @brief The version index is made of 3 numbers.
 */
#define VERSION_INDEX_LEN  3

/**
 * @def VERSION_INDEX_FILENAME
 * @brief Name of the file where the version of the configuration is written.
 */
#define VERSION_INDEX_FILENAME "rxver.dat"


void InitGuiConfiguration(TNGeneConfiguration *cfg)
{
	TGuiConfiguration *guiCfg = &(cfg->guiParameters);

	/* Channel parameters */
	guiCfg->automaticConfigurationFlag = TRUE_FLAG;
	guiCfg->gpsCoherentAcqIntTime = 0;
	guiCfg->galileoCoherentTrkIntTime = 0;
	guiCfg->galileoCoherentAcqIntTime = 0;
	guiCfg->egnosCoherentAcqIntTime = 0;
	guiCfg->numberOfExcludedPrns = 0;
	guiCfg->excludedPrns.head = NULL;
	guiCfg->manuallyConfiguredChannels = NULL;
	/* IF ANALYSIS and LOG PARAMETERS */
	guiCfg->spectrumSamples = 0;
	guiCfg->spectrumWindows = 0;
	guiCfg->spectrumOverlap = 0;
	guiCfg->spectrumZeroPadding = 0;
	guiCfg->spectrumWeightFunction = 0;
	guiCfg->histSamples = 0;
	guiCfg->histBits = 0;
	guiCfg->outputPathLength = 0;
	guiCfg->outputPath = NULL;
	guiCfg->ifGenerationFlag = FALSE_FLAG;
	/* MULTIPLE CORRELATION PARAMETERS */
	guiCfg->multipleCorrelatorsFlag = FALSE_FLAG;
	guiCfg->numberOfMultipleCorrelators = 0;
	guiCfg->multipleCorrelatorsSpacing = 0;
}


void DeleteGuiConfiguration(TNGeneConfiguration *cfg)
{
	TGuiConfiguration *guiCfg = &(cfg->guiParameters);

	if((guiCfg->automaticConfigurationFlag==TRUE_FLAG)&&(guiCfg->numberOfExcludedPrns>0))
	{
		DestroyPrnList(&guiCfg->excludedPrns);
		guiCfg->numberOfExcludedPrns = 0;
	}
	if((guiCfg->automaticConfigurationFlag==FALSE_FLAG)&&(guiCfg->manuallyConfiguredChannels!=NULL))
	{
		free(guiCfg->manuallyConfiguredChannels);
		guiCfg->manuallyConfiguredChannels=NULL;
	}
	if((guiCfg->outputPathLength>0)&&(guiCfg->outputPath!=NULL))
	{
		free(guiCfg->outputPath);
		guiCfg->outputPath=NULL;
	}
}

#ifndef DISABLE_CONFIGURATION_CHECK
int ConfigurationFilesValid(TNGeneConfiguration *cfg)
{

	char cfgFilename[2*MAX_ROW_LENGTH];
	FILE *fp;
	int n;
	unsigned char currentVer[VERSION_INDEX_LEN];
	unsigned char neededVer[VERSION_INDEX_LEN];

	// find the receiver version for the active configuration folder
	// if not available the configuration folder must be upgraded
	sprintf(cfgFilename,"%s/%s",cfg->otherDataPath,VERSION_INDEX_FILENAME);
	fp = fopen(cfgFilename,"rb");
	if(fp==NULL)
	{
		SwrxLog(NGENE_LOG_SEVERITY_NOTE,"Current configuration version not available");
		return FALSE_FLAG;
	}
	n = fread(currentVer,sizeof(unsigned char),VERSION_INDEX_LEN,fp);
	fclose(fp);
	if(n<VERSION_INDEX_LEN)
	{
		return FALSE_FLAG;
	}

	// find the receiver version required by the current installation
	// if not available the configuration folder is valid
	sprintf(cfgFilename,"%s/sampleConfigurationFiles/otherData/%s",cfg->homePath,VERSION_INDEX_FILENAME);
	fp = fopen(cfgFilename,"rb");
	if(fp==NULL)
	{
		SwrxLog(NGENE_LOG_SEVERITY_NOTE,"Required configuration version not available");
		return TRUE_FLAG;
	}
	n = fread(neededVer,sizeof(unsigned char),VERSION_INDEX_LEN,fp);
	fclose(fp);
	if(n<VERSION_INDEX_LEN)
	{
		return TRUE_FLAG;
	}

	// check the version indexes
	if((neededVer[0]>currentVer[0]) ||
	   ((neededVer[0]==currentVer[0]) && (neededVer[1]>currentVer[1])) ||
	   ((neededVer[0]==currentVer[0]) && (neededVer[1]==currentVer[1]) && (neededVer[2]>currentVer[2])))
	{
		SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,"Check the configuration version. Current %d.%d.%d VS Required %d.%d.%d",
				currentVer[0],currentVer[1],currentVer[2],neededVer[0],neededVer[1],neededVer[2]);
		return FALSE_FLAG;
	}

	return TRUE_FLAG;
}
#endif


int ManageLogsConfiguration(TNGeneConfiguration *cfg)
{
	TNGeneLogConfiguration *logCfg = &(cfg->logConfiguration);
	char cfgFilename[MAX_ROW_LENGTH];
	char tag[MAX_TAG_LEN];
	//char string[MAX_ROW_LENGTH];

	cfgFilename[0] = '\0';
	strcat(cfgFilename, cfg->configurationPath);
	strcat(cfgFilename, "Logs.conf");

    /* acquisition*/
	strcpy(tag, "Enable Acquisition Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableAcqLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableAcqLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enableAcqLog = (logCfg->enableAcqLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}	

	/* tracking */
	strcpy(tag, "Enable Tracking Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableTrkLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableTrkLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enableTrkLog = (logCfg->enableTrkLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}	

	/* navigation message in custom format */
	strcpy(tag, "Enable Navigation Message Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableNavMesgLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableNavMesgLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enableNavMesgLog = (logCfg->enableNavMesgLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}
	
	/* navigation message in IFEN format */
	strcpy(tag, "Enable Navigation Message IFEN Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableNavMesgIfenLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableNavMesgIfenLog = FALSE_FLAG;
	}
	else
	{
		logCfg->enableNavMesgIfenLog = (logCfg->enableNavMesgIfenLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}

	/* YUMA almanac */
	strcpy(tag, "Enable Almanac Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableAlmanacLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableAlmanacLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enableAlmanacLog = (logCfg->enableAlmanacLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}
	
	/* OSNMA */
	strcpy(tag, "Enable OSNMA Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableOsnmaLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableOsnmaLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enableOsnmaLog = (logCfg->enableOsnmaLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}
	//
	strcpy(tag, "Enable OSNMA Simple Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableOsnmaSimpleLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableOsnmaSimpleLog = FALSE_FLAG;
	}
	else
	{
		logCfg->enableOsnmaSimpleLog = (logCfg->enableOsnmaSimpleLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}


	
	/* PVT */
	strcpy(tag, "Enable PVT Text Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enablePvtTextLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enablePvtTextLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enablePvtTextLog = (logCfg->enablePvtTextLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}
	//
	strcpy(tag, "Enable PVT Binary Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enablePvtBinLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enablePvtBinLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enablePvtBinLog = (logCfg->enablePvtBinLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}
	//
	strcpy(tag, "Enable GGTO Log");
	if(GetVal(cfgFilename,tag,(void *)&(logCfg->enableGgtoLog),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Error while parsing configuration file <%s>: tag <%s> not found. Default value used.",cfgFilename,tag);
		logCfg->enableGgtoLog = FALSE_FLAG;
	}
	else 
	{
		logCfg->enableGgtoLog = (logCfg->enableGgtoLog>0) ? TRUE_FLAG : FALSE_FLAG;
	}
	
	/* RINEX */
	ManageRinexConfiguration(cfg,cfgFilename);

	/* NMEA */
	ManageNmeaConfiguration(cfg,cfgFilename);

	return SUCCESS_RETURN;
}


int ParseConfigurationFiles(TNGeneConfiguration *cfg)
{
	char cfgFilename[MAX_ROW_LENGTH];
	char cfgFilenameManualConfiguration[MAX_ROW_LENGTH];
	char tag[MAX_TAG_LEN];
	char string[MAX_ROW_LENGTH];
	int index;
	TManualChannelConfiguration *chCfg;
	int bytesRead;
	size_t nBytes = 100;
    char *line;
    FILE *fin;

	#ifndef DISABLE_CONFIGURATION_CHECK
    /* check if the current configuration folder is valid, terminates if it must be updated! */
    if(ConfigurationFilesValid(cfg)==FALSE_FLAG)
    {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,"The current configuration folder %s is out-of-date!",cfg->configurationPath);
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Update the settings using the templates in sampleConfigurationFiles.");
    	CloseNGene(cfg,EXIT_FAILURE);
    }
	#endif

	/* give a feedback to the user */
	SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,"home folder:          %s",cfg->homePath);
	SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,"configuration folder: %s",cfg->configurationPath);
	SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,"other data folder:    %s",cfg->otherDataPath);
	SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,"receiver log folder:  %s",cfg->dumpPathCurrent);

	/* GNSS_RX.conf */
	cfgFilename[0] = '\0';
	strcat(cfgFilename, cfg->configurationPath);
	strcat(cfgFilename, "GNSS_RX.conf");
	cfg->sigeNativeIq = 0;
	cfg->sigeIqIntermediateFrequency = 38400.0;
	GetVal(cfgFilename, "SiGe native IQ", &cfg->sigeNativeIq, F_INTEGER, 1);
	GetVal(cfgFilename, "SiGe IQ intermediate frequency (Hz)", &cfg->sigeIqIntermediateFrequency, F_DOUBLE, 1);
	/**/
	strcpy(tag, "Used FrontEnd");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->usedFrontEnd),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Error while parsing configuration file <%s>: tag <%s> not found",cfgFilename,tag);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	/**/
	strcpy(tag, "Input method");
    if (cfg->frontEndOverride) cfg->usedFrontEnd = cfg->frontEndOverride;
	if(GetVal(cfgFilename,tag,(void *)&(cfg->inputMethod),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Error while parsing configuration file <%s>: tag <%s> not found",cfgFilename,tag);
		CloseNGene(cfg,EXIT_FAILURE);
	}
    if (cfg->inputFileOverride) cfg->inputMethod = INPUT_FILE;
	if(cfg->inputMethod==USB_FRONTEND)
	{
		cfg->sampleFilename = NULL;
		SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,"Running front end mode from USB front end");
	}
	else
	{
		cfg->inputMethod = INPUT_FILE;
		strcpy(tag, "Input file");
        if (cfg->inputFileOverride) {
            if (strlen(cfg->inputFileOverride) >= sizeof(string)) {
                SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Input file path is too long");
                CloseNGene(cfg,EXIT_FAILURE);
            }
            strcpy(string,cfg->inputFileOverride);
        }
		else if(GetVal(cfgFilename,tag,(void *)string,F_STRING,1) == FAILURE_RETURN)
		{
			SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Error while parsing configuration file <%s>: tag <%s> not found",cfgFilename,tag);
			CloseNGene(cfg,EXIT_FAILURE);
		}
		cfg->sampleFilename = (char *)malloc((strlen(string)+1)*sizeof(char));
		if(cfg->sampleFilename==NULL)
		{
			SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Malloc error in ParseConfigurationFiles (tag <%s>)",tag);
			CloseNGene(cfg,EXIT_FAILURE);
		}
		memcpy(cfg->sampleFilename,string,strlen(string));
		cfg->sampleFilename[strlen(string)]='\0';
		#ifndef FAKE_OSNMA_RECEIVER
		if(cfg->userInterface!=RX_UI_GUI)
		{
			SwrxLog(NGENE_LOG_SEVERITY_IMPORTANT_INFO,"Running off-line mode from file <%s>",cfg->sampleFilename);
		}
		#endif
	}
	/**/
	strcpy(tag, "Number of supported GPS channels");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->numberOfGpsChannels),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Error while parsing configuration file <%s>: tag <%s> not found",cfgFilename,tag);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	strcpy(tag, "Number of supported Galileo channels");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->numberOfGalileoChannels),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Error while parsing configuration file <%s>: tag <%s> not found",cfgFilename,tag);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	strcpy(tag, "Number of supported EGNOS channels");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->numberOfEgnosChannels),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Error while parsing configuration file <%s>: tag <%s> not found",cfgFilename,tag);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	if(cfg->numberOfEgnosChannels>1)
	{
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Please set only one EGNOS channel");
		CloseNGene(cfg,EXIT_FAILURE);
	}
	cfg->numberOfGalileoE5aChannels = 0;
	cfg->numberOfChannels = cfg->numberOfGpsChannels + cfg->numberOfGalileoChannels + cfg->numberOfEgnosChannels;
	/**/
	strcpy(tag, "Receiver clock emulation");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->receiverClockEmulation),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_WARNING,"Parameter <%s> not found in file <%s>. Disabled by default.",tag,cfgFilename);
		cfg->receiverClockEmulation = FALSE_FLAG;
	}
	if((cfg->receiverClockEmulation!=TRUE_FLAG) && (cfg->receiverClockEmulation!=TRUE_FLAG))
	{
		cfg->receiverClockEmulation = FALSE_FLAG;
	}
	strcpy(tag, "Receiver clock error");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->receiverClockError),F_INTEGER,1) == FAILURE_RETURN)
	{
		cfg->receiverClockEmulation = FALSE_FLAG;
		cfg->receiverClockError = 0;
	}
	strcpy(tag, "Enable all Galileo PRNs");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->allGalileoPrnsEnabled),F_INTEGER,1) == FAILURE_RETURN)
	{
		cfg->allGalileoPrnsEnabled = FALSE_FLAG;
	}
	if(cfg->allGalileoPrnsEnabled>0)
	{
		cfg->allGalileoPrnsEnabled = TRUE_FLAG;
	}
	else
	{
		cfg->allGalileoPrnsEnabled = FALSE_FLAG;
	}

	/*******************************************************************/
	/* Channel parameters */
	strcpy(tag, "Automatic Configuration");
	if(GetVal(cfgFilename,tag,(void *)&(cfg->automaticConfigurationFlag),F_INTEGER,1) == FAILURE_RETURN)
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Error while parsing configuration file <%s>: tag <%s> not found",cfgFilename,tag);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	if(cfg->automaticConfigurationFlag==FALSE_FLAG)
	{
		cfgFilenameManualConfiguration[0] = '\0';
		strcat(cfgFilenameManualConfiguration, cfg->configurationPath);
		strcat(cfgFilenameManualConfiguration, "PRN_List.conf");

		fin = fopen(cfgFilenameManualConfiguration,"r");
		if(fin == NULL)
		{
			SwrxLog(NGENE_LOG_SEVERITY_ERROR, "ERROR: file %s not found",cfgFilenameManualConfiguration);
			return FAILURE_RETURN;
		}
		line = (char *) malloc (nBytes + 1);
		bytesRead = getline (&line, &nBytes, fin);
		if (bytesRead == -1)
		{
			SwrxLog(NGENE_LOG_SEVERITY_ERROR, "ERROR: file %s not written correctly",cfgFilenameManualConfiguration);

		}
		sscanf(&(line[0]),"%d", &cfg->numberOfChannels);
		cfg->numberOfGpsChannels=0;
		cfg->numberOfGalileoChannels=0;
		cfg->numberOfEgnosChannels=0;
		cfg->manuallyConfiguredChannels = (TManualChannelConfiguration *)malloc(cfg->numberOfChannels*sizeof(TManualChannelConfiguration));
		for(index=0;index<cfg->numberOfChannels;index++)
		{
			chCfg = &(cfg->manuallyConfiguredChannels[index]);
			/**/
			bytesRead = getline (&line, &nBytes, fin);
			if (bytesRead == -1)
			{
				SwrxLog(NGENE_LOG_SEVERITY_ERROR, "ERROR: file %s not written correctly",cfgFilenameManualConfiguration);

			}
			/**/
			if(strncmp(line,"GPS",3)==0)
			{
				chCfg->satType = GPS_SAT;
				sscanf(&(line[3]),"%d %f %f %f %f %f %f", &chCfg->satId, &chCfg->dllBw,&chCfg->pllBw,&chCfg->coherentTrkIntTime,&chCfg->coherentAcqIntTime,&chCfg->nonCoherentAcqIntTime,&chCfg->spacing);
			}
			else if(strncmp(line,"EGNOS",5)==0)
			{
				chCfg->satType = EGNOS_SAT;
				sscanf(&(line[5]),"%d %f %f %f %f %f %f",&chCfg->satId, &chCfg->dllBw,&chCfg->pllBw,&chCfg->coherentTrkIntTime,&chCfg->coherentAcqIntTime,&chCfg->nonCoherentAcqIntTime,&chCfg->spacing);
			}
			else /* if(strncmp(prnDescription,"Galileo",7)==0) */
			{
				chCfg->satType = GALILEO_SAT;
					sscanf(&(line[7]),"%d %f %f %f %f %f %f",&chCfg->satId, &chCfg->dllBw,&chCfg->pllBw,&chCfg->coherentTrkIntTime,&chCfg->coherentAcqIntTime,&chCfg->nonCoherentAcqIntTime,&chCfg->spacing);
			}
			if(chCfg->satType==GPS_SAT)
			{
				cfg->numberOfGpsChannels++;
			}
			else if(chCfg->satType==EGNOS_SAT)
			{
				cfg->numberOfEgnosChannels++;
			}
			else /* if(chCfg->satType==GALILEO_SAT) */
			{
				cfg->numberOfGalileoChannels++;
			}
		}
		fclose(fin);
		free(line);
	}

	/******************************************************************/
	/* PVT.conf */
	ManagePvtConfiguration(cfg);
	/* Acquisition.conf */
	ManageAcqConfiguration(cfg);
	/* Tracking.conf */
	ManageTrkConfiguration(cfg);
	/* Warm.conf */
	ManageWarmStartConfiguration(cfg);
	/* Front_End.conf */
	ManageUsbFeConfiguration(cfg);
	/* Navigation Message */
	ManageNavigationMessageConfiguration(cfg);
	/* Init GUI configuration with dummy values */
	InitGuiConfiguration(cfg);
	/* Manage NCurses */
	cfg->ncursesDataInitialized = FALSE_FLAG;
	/* Manage NGene2 logs configuration */
	ManageLogsConfiguration(cfg);

	/* configuration is valid */
	cfg->configurationValid = TRUE_FLAG;

	return SUCCESS_RETURN;
}


void PlotConfiguration(TNGeneConfiguration *cfg)
{
	TPvtConfiguration *pvtCfg = &(cfg->pvtConfiguration);
	TTrkConfiguration *trkCfg = &(cfg->trkConfiguration);
	TAcqConfiguration *acqCfg = &(cfg->acqConfiguration);
	TWarmStartConfiguration *warmCfg = &(cfg->warmConfiguration);
	TGuiConfiguration *guiCfg = &(cfg->guiParameters);
	int index;
	TPrnListElement *excludedPrn;
	char msg[MAX_ROW_LENGTH];

	if(cfg->verbose==FALSE_FLAG)
	{
		return;
	}

	if(cfg->userInterface==RX_UI_GUI)
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "%s current configuration received from port %u", cfg->progname,cfg->guiComPort);
	else
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "%s current configuration read from folder %s", cfg->progname,cfg->configurationPath);

	if(cfg->inputMethod==USB_FRONTEND)
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "Input from USB front-end");
	else
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "Input from binary file %s",cfg->sampleFilename);

	/* front-end */
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "**** Data type");
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     %s front-end",cfg->fePars->name);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     fif %f",cfg->fif);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     fs %f",cfg->fs);
	if(cfg->fePars->meanValue>0)
	{
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     uchar format with %u used bits and mean value %u",cfg->fePars->resolution,cfg->fePars->meanValue);
	}
	else
	{
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     char format with %u used bits",cfg->fePars->resolution);
	}
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     VID/PID %04x/%04x",cfg->fePars->vid,cfg->fePars->pid);

	/* channels */
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "**** %d channels",cfg->numberOfChannels);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     %d GPS",cfg->numberOfGpsChannels);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     %d Galileo",cfg->numberOfGalileoChannels);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     %d EGNOS",cfg->numberOfEgnosChannels);

	/* Acquisition */
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "**** Acquisition parameters");
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS coarse acquisition false detection probability %g",acqCfg->gpsFalseDetectionProb);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS coarse acquisition tests Doppler up to %f Hz with step %f Hz",acqCfg->gpsMaximumDoppler,acqCfg->gpsDopplerStep);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS coarse acquisition tests code delay with step %f chips",acqCfg->gpsCodeDelayStep);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS confirmation %d on %d",acqCfg->gpsConfirmationM,acqCfg->gpsConfirmationN);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS non-coherent sums %d",acqCfg->gpsNumOfNonCohSums);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo coarse acquisition false detection probability %g",acqCfg->galileoFalseDetectionProb);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo coarse acquisition tests Doppler up to %f Hz with step %f Hz",acqCfg->galileoMaximumDoppler,acqCfg->galileoDopplerStep);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo coarse acquisition tests code delay with step %f chips",acqCfg->galileoCodeDelayStep);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo confirmation %d on %d",acqCfg->galileoConfirmationM,acqCfg->galileoConfirmationN);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo non-coherent sums %d",acqCfg->galileoNumOfNonCohSums);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS coarse acquisition false detection probability %g",acqCfg->egnosFalseDetectionProb);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS coarse acquisition tests Doppler up to %f Hz with step %f Hz",acqCfg->egnosMaximumDoppler,acqCfg->egnosDopplerStep);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS coarse acquisition tests code delay with step %f chips",acqCfg->egnosCodeDelayStep);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS confirmation %d on %d",acqCfg->egnosConfirmationM,acqCfg->egnosConfirmationN);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS non-coherent sums %d",acqCfg->egnosNumOfNonCohSums);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Doppler refinement %f Hz with step %f Hz",acqCfg->dopplerDeltaRefinement,acqCfg->dopplerStepRefinement);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Code delay refinement %f chips with step %d samples",acqCfg->codeDelayDeltaRefinement,acqCfg->codeDelayStepRefinement);

	/* Tracking */
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "**** Tracking parameters");
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Minimum CNo %f dBHz",trkCfg->cutoffCno);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS user integration period %d ms",trkCfg->gpsIntegrationPeriod);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS DLL bandwidth %f Hz (gain %f)",trkCfg->gpsDllBw,trkCfg->gpsDllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS FLL bandwidth %f Hz (gain %f)",trkCfg->gpsFllBw,trkCfg->gpsFllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS PLL bandwidth %f Hz (gain %f)",trkCfg->gpsPllBw,trkCfg->gpsPllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     GPS spacing %f chips",trkCfg->gpsSpacing);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS user integration period %d ms",trkCfg->egnosIntegrationPeriod);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS DLL bandwidth %f Hz (gain %f)",trkCfg->egnosDllBw,trkCfg->egnosDllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS FLL bandwidth %f Hz (gain %f)",trkCfg->egnosFllBw,trkCfg->egnosFllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS PLL bandwidth %f Hz (gain %f)",trkCfg->egnosPllBw,trkCfg->egnosPllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS spacing %f chips",trkCfg->egnosSpacing);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo DLL bandwidth %f Hz (gain %f)",trkCfg->galileoDllBw,trkCfg->galileoDllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo FLL bandwidth %f Hz (gain %f)",trkCfg->galileoFllBw,trkCfg->galileoFllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo PLL bandwidth %f Hz (gain %f)",trkCfg->galileoPllBw,trkCfg->galileoPllGain);
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Galileo spacing %f chips",trkCfg->galileoSpacing);
	if(cfg->logConfiguration.enableTrkLog==TRUE_FLAG)
	{
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "  ** High rate correlators file will be created");
	}

	/* PVT */
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "**** PVT parameters");
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     PVT rate %f Hz",pvtCfg->pvtRate);
	if(pvtCfg->pvtMethod==KALMAN_BASED_PVT)
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Kalman filter based PVT");
	else
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Least Mean Squares PVT");
	if(pvtCfg->csmFilterLen>0)
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Carrier smoothing (M=%d)",pvtCfg->csmFilterLen);
	if(pvtCfg->egnosFlag==TRUE_FLAG)
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     EGNOS correction applied");
	SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Maximum GDOP %f",pvtCfg->maxGdop);
	if(pvtCfg->elevationMaskFlag==TRUE_FLAG)
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     Minimum satellite elevation %f radians",pvtCfg->elevationMask);

	/* Warm Start */
	if(warmCfg->enableWarmStart==TRUE_FLAG)
	{
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "**** Warm start");
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     known position %f deg N, %f deg E",warmCfg->warmLatitude,warmCfg->warmLongitude);
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     known date & time %d/%d/%d %d:%d:%d",warmCfg->warmDay,warmCfg->warmMonth,warmCfg->warmYear,warmCfg->warmHour,warmCfg->warmMinute,warmCfg->warmSecond);
	}

	/* GUI */
	if(cfg->userInterface==RX_UI_GUI)
	{
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "**** GUI specific parameters");
		if(guiCfg->automaticConfigurationFlag==TRUE_FLAG)
		{
			SwrxLog( NGENE_LOG_SEVERITY_NOTE, "  ** Automatic configuration");
			if(guiCfg->numberOfExcludedPrns>0)
			{
				SwrxLog( NGENE_LOG_SEVERITY_NOTE, "  ** PRNs excluded from elaboration");
				excludedPrn = guiCfg->excludedPrns.head;
				while(excludedPrn!=NULL)
				{
					FromPrnToString(excludedPrn->prnConstellation, excludedPrn->prn, msg);
					SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     %s",msg);
					excludedPrn = excludedPrn->nextPrn;
				}
			}
		}
		else
		{
			SwrxLog( NGENE_LOG_SEVERITY_NOTE, "  ** Manual configuration");
			for(index=0;index<cfg->numberOfChannels;index++)
			{
				FromPrnToString(guiCfg->manuallyConfiguredChannels[index].satType, guiCfg->manuallyConfiguredChannels[index].satId, msg);
				SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     ch. %2d  %s",index,msg);
			}
		}
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "  ** SPECTRUM size %d",guiCfg->spectrumSamples);
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "              zero padding %d",guiCfg->spectrumZeroPadding);
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "              windows %d",guiCfg->spectrumWindows);
		SwrxLog( NGENE_LOG_SEVERITY_NOTE, "              overlap %d",guiCfg->spectrumOverlap);
		if(guiCfg->multipleCorrelatorsFlag==TRUE_FLAG)
		{
			SwrxLog( NGENE_LOG_SEVERITY_NOTE, "  ** Multiple correlators");
			SwrxLog( NGENE_LOG_SEVERITY_NOTE, "     %d correlators spaced by %d samples",guiCfg->numberOfMultipleCorrelators,guiCfg->multipleCorrelatorsSpacing);
		}
	}
}


void DeleteConfiguration(TNGeneConfiguration *cfg)
{
	if(cfg->configurationValid==TRUE_FLAG)
	{
		pthread_mutex_destroy(&(cfg->stdLogMutex));
		if((cfg->stdlog!=NULL) && (cfg->stdlog!=stdout))
		{
			fclose(cfg->stdlog);
			cfg->stdlog=NULL;
		}
		if(cfg->sampleFilename!=NULL)
		{
			free(cfg->sampleFilename);
		}
		if(cfg->automaticConfigurationFlag==FALSE_FLAG)
		{
			free(cfg->manuallyConfiguredChannels);
		}
		DeleteWarmStartConfiguration(cfg);
		DeleteUsbFeConfiguration(cfg);
		DeleteGuiConfiguration(cfg);
		cfg->configurationValid=FALSE_FLAG;
	}
}


void CompleteConfiguration(TNGeneConfiguration *cfg)
{
	int index = 0;

	/* complete FE configuration and verify the validity */
	while((index<cfg->usbFeParameters.numberOfSupportedFes)&&(cfg->usbFeParameters.feList[index].index!=cfg->usedFrontEnd))
	{
		index++;
	}
	if(index<cfg->usbFeParameters.numberOfSupportedFes)
	{
		cfg->fePars = &(cfg->usbFeParameters.feList[index]);
	}
	else
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Unsupported front-end ID %d: select SiGe (1 or 6) or HackRF (7)",cfg->usedFrontEnd);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	if((cfg->fePars->vid==0) && (cfg->fePars->pid==0) && (cfg->inputMethod==USB_FRONTEND))
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Invalid configuration: VID/PID 0000/0000 for the %s front-end (must work from file)",cfg->fePars->name);
		CloseNGene(cfg,EXIT_FAILURE);
	}
	/* other parameters */
	cfg->fs=cfg->fePars->samplingRate;
	cfg->fif=cfg->fePars->intermediateFrequency;
    cfg->nativeIq = cfg->usedFrontEnd == NGENE_FE_HACKRF || cfg->sigeNativeIq;
	if(cfg->sigeNativeIq && cfg->usedFrontEnd != NGENE_FE_HACKRF)
	{
		if(cfg->sigeNativeIq != 1 || cfg->usedFrontEnd != NGENE_FE_SIGE ||
		   !isfinite(cfg->sigeIqIntermediateFrequency) ||
		   fabs(cfg->sigeIqIntermediateFrequency) >= cfg->fs/4.0)
		{
			SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Native IQ requires SiGe ID 1 and a valid complex IF");
			CloseNGene(cfg,EXIT_FAILURE);
		}
		cfg->fs *= 0.5; /* Two transport bytes represent one complex sample. */
		cfg->fif = cfg->sigeIqIntermediateFrequency;
	}
    if (cfg->usedFrontEnd == NGENE_FE_HACKRF &&
        (cfg->fs != 8183800.0 || cfg->fif != 38400.0 ||
         cfg->fePars->dopplerCorrection != -1 || cfg->fePars->resolution != 8 ||
         cfg->fePars->meanValue != 0)) {
        SwrxLog(NGENE_LOG_SEVERITY_ERROR,
                "HackRF comparison profile requires fs=8183800, IF=38400, correction=-1, signed CI8");
        CloseNGene(cfg, EXIT_FAILURE);
    }
	cfg->ts=1.0/cfg->fs;
	SwrxLog(NGENE_LOG_SEVERITY_INFO, "Front end: %s (ID %d), fs %.0f Hz, IF %.0f Hz",
	        cfg->fePars->name, cfg->usedFrontEnd, cfg->fs, cfg->fif);
	SwrxLog(NGENE_LOG_SEVERITY_INFO, "Input samples: %s", cfg->nativeIq ?
	        "native complex I/Q (one pair per sample)" : "real IF");
	cfg->samplesPerMillisecond = cfg->fs/SECONDS_TO_MILLISECONDS;
	cfg->samplesPerChip = cfg->fs/GPS_CODE_RATE;
	cfg->pvtPeriodMilliseconds = (unsigned int)ceil(SECONDS_TO_MILLISECONDS/cfg->pvtConfiguration.pvtRate);
	cfg->pvtPeriodSeconds = 1.0/cfg->pvtConfiguration.pvtRate;
	if(cfg->inputMethod==INPUT_FILE)
	{
		cfg->receiverClockEmulation = FALSE_FLAG; // if the input is from file, PC date/time are not used
	}
	if(cfg->fs<=LOW_FS)
	{
		#ifdef CSC_DISCRIMINATOR_FUNCTION
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Sampling frequency too low, not compatible with the CSC discriminator");
		CloseNGene(cfg,EXIT_FAILURE);
		#else
		cfg->lowFsElaboration = TRUE_FLAG;
		SwrxLog(NGENE_LOG_SEVERITY_INFO,"Sampling frequency requires a special tracking implementation\n");
		#endif
	}
	else
	{
		cfg->lowFsElaboration = FALSE_FLAG;
		SwrxLog(NGENE_LOG_SEVERITY_NOTE,"Sampling frequency allows the standard tracking implementation\n");
	}
	if(cfg->fs<MINIMUM_FS_FOR_GPS)
	{
		cfg->disableGpsElaboration = TRUE_FLAG;
		SwrxLog(NGENE_LOG_SEVERITY_WARNING, "Sampling frequency (%.1f MHz) too low to allow the elaboration of GPS signal (minimum %.1f MHz)",cfg->fs/FROM_MICROSECONDS_TO_SECONDS,MINIMUM_FS_FOR_GPS/FROM_MICROSECONDS_TO_SECONDS);
	}
	else
	{
		cfg->disableGpsElaboration = FALSE_FLAG;
	}
	if(cfg->fs<MINIMUM_FS_FOR_GAL)
	{
		cfg->disableGalElaboration = TRUE_FLAG;
		SwrxLog(NGENE_LOG_SEVERITY_WARNING, "Sampling frequency (%.1f MHz) too low to allow the elaboration of Galileo signal (minimum %.1f MHz)",cfg->fs/FROM_MICROSECONDS_TO_SECONDS,MINIMUM_FS_FOR_GAL/FROM_MICROSECONDS_TO_SECONDS);
	}
	else
	{
		cfg->disableGalElaboration = FALSE_FLAG;
	}
	if((cfg->disableGpsElaboration==TRUE_FLAG) && (cfg->disableGalElaboration==TRUE_FLAG))
	{
		SwrxLog(NGENE_LOG_SEVERITY_ERROR,"Sampling frequency too low, no elaboration is possible");
		CloseNGene(cfg,EXIT_FAILURE);
	}
	// apply fs limitations to the acqusition configuration
	CompleteAcqConfiguration(cfg);
}

