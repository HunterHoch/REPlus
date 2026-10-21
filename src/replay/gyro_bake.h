#pragma once
#include "replay/gyro_take.h"
#include <string>
namespace gyro {
bool bakeActive();
bool bakeHasChanges();
const char* bakeStatus();
void beginBake(const std::vector<Sample>& samples,const std::string& scope,float returnTime);
void pumpBake(const std::string& scope,float clock);
void cancelBake(const std::string& scope);
void undoBake(const std::string& scope);
}
