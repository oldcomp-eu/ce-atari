#ifndef MEDIASTREAMING_H
#define MEDIASTREAMING_H

#include <stdio.h>
#include "acsidatatrans.h"
#include "translated/translateddisk.h"

#define MEDIASTREAMING_MAXSTREAMS	4

class MediaStream
{
public:
	MediaStream(void);
	~MediaStream();
	bool isFree(void);
	bool open(const char * filename);
	int getInfos(BYTE * buffer, int bufferlen);
	int read(BYTE * buffer, int bufferlen);
	void close(void);
private:
	FILE * f;
	bool isPipe;
};

class MediaStreaming
{
public:
	MediaStreaming(void);
	~MediaStreaming();

	void processCommand(BYTE *command, AcsiDataTrans *dataTrans);

	void setTranslatedDisk(TranslatedDisk *td) { this->translated = td; };
private:
	void openStream(AcsiDataTrans *dataTrans);
	void getStreamInfo(BYTE streamHandle, AcsiDataTrans *dataTrans);
	void readStream(BYTE arg, AcsiDataTrans *dataTrans);
	void closeStream(BYTE streamHandle, AcsiDataTrans *dataTrans);

// properties
	MediaStream streams[MEDIASTREAMING_MAXSTREAMS];
	TranslatedDisk *translated;
};

#endif // MEDIASTREAMING_H
