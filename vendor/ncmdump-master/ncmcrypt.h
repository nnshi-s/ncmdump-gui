#pragma once

#include "aes.h"
#include "cJSON.h"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;


class NeteaseMusicMetadata {

private:
	std::string mAlbum;
	std::string mArtist;
	std::string mFormat;
	std::string mName;
	int mDuration = 0;
	int mBitrate = 0;

private:
	cJSON* mRaw = nullptr;

public:
	NeteaseMusicMetadata(cJSON*);
	~NeteaseMusicMetadata();
    const std::string& name() const { return mName; }
    const std::string& album() const { return mAlbum; }
    const std::string& artist() const { return mArtist; }
    const std::string& format() const { return mFormat; }
    const int duration() const { return mDuration; }
    const int bitrate() const { return mBitrate; }

};

class NeteaseCrypt {

private:
	static const unsigned char sCoreKey[17];
	static const unsigned char sModifyKey[17];
	static const unsigned char mPng[8];
	enum NcmFormat { MP3, FLAC };

private:
	fs::path mFilepath;
	fs::path mDumpFilepath;
	NcmFormat mFormat;
	std::string mImageData;
	std::ifstream mFile;
	unsigned char mKeyBox[256];
	NeteaseMusicMetadata* mMetaData = nullptr;

private:
	bool isNcmFile();
	bool openFile(const fs::path &);
	int read(char *s, std::streamsize n);
	void buildKeyBox(unsigned char *key, int keyLen);
	std::string mimeType(std::string& data);

public:
	const fs::path& filepath() const { return mFilepath; }
	const fs::path& dumpFilepath() const { return mDumpFilepath; }
	void dumpFilePath(const fs::path &s) { mDumpFilepath = s; }

public:
	NeteaseCrypt(const fs::path &);
	~NeteaseCrypt();

public:
	void Dump();
	void FixMetadata();
};