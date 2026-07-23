#include "hook.h"
#include "log.h"
#include "settings.h"

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		Settings::GetSingleton()->Load();
		Hook::RegisterEvents();
		break;
	case SKSE::MessagingInterface::kPostLoad:
		break;
	case SKSE::MessagingInterface::kPreLoadGame:
		// Clear in-memory hate/focus before actors are restored from the save.
		// Stale focus across death+reload can permanently suppress player detection.
		Hook::ResetRuntimeState();
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
		// Belt-and-suspenders: ensure no mid-load leftovers remain.
		Hook::ResetRuntimeState();
		break;
	case SKSE::MessagingInterface::kNewGame:
		Hook::ResetRuntimeState();
		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse);
	SetupLog();

	SKSE::AllocTrampoline(64);
	Hook::Install();

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

	return true;
}