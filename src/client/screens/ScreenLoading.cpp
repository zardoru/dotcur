#include <memory>
#include <atomic>
#include <thread>
#include <string>
#include <filesystem>
#include <map>
#include <functional>

#include <game/GameConstants.h>
#include "../game/PlayscreenParameters.h"
#include "../game/GameState.h"

#include "../structure/Screen.h"
#include "../structure/SceneEnvironment.h"

#include "LuaManager.h"
#include "ScreenLoading.h"
#include "Logging.h"
#include "../game/Game.h"

/// @themescript screenloading.lua

class LoadScreenThread
{
    std::atomic<bool>& mFinished;
    Screen* mScreen;
public:
    LoadScreenThread(std::atomic<bool>& status, Screen* screen) : mFinished(status), mScreen(screen) {};
    void DoLoad() const
    {
        mFinished = false;
        try
        {
            mScreen->LoadResources();
        }
        catch (InterruptedException &)
        {
            Log::Printf("Thread was interrupted.\n");
        }
        catch (std::exception &e)
        {
            Log::LogPrintf("Exception while loading: %s\n", e.what());
        }
        mFinished = true;
    }
};

ScreenLoading::ScreenLoading(std::shared_ptr<Screen> _Next) : Screen("ScreenLoading", false)
{
    Next = _Next;
    LoadThread = nullptr;
    Running = true;
    ThreadInterrupted = false;
	/// Global gamestate.
	// @autoinstance Global
    GameState::GetInstance().InitializeLua(Animations->GetEnv()->GetState());

    Animations->Preload(GameState::GetInstance().GetSkinFile("screenloading.lua"), "Preload");
    Animations->Initialize("", false);

    IntroDuration = std::max(Animations->GetEnv()->GetGlobalD("IntroDuration"), 0.0);
    ExitDuration = std::max(Animations->GetEnv()->GetGlobalD("ExitDuration"), 0.0);

    ChangeState(StateIntro);
}

void ScreenLoading::OnIntroBegin()
{
    //WindowFrame.SetLightMultiplier(0.8f);
    //WindowFrame.SetLightPosition(glm::vec3(0, -0.5, 1));
}

void ScreenLoading::Init()
{
    LoadThread = std::make_shared<std::thread>(&LoadScreenThread::DoLoad, LoadScreenThread(FinishedLoading, Next.get()));
}

void ScreenLoading::OnExitEnd()
{
    Screen::OnExitEnd();

    //WindowFrame.SetLightMultiplier(1);
    //WindowFrame.SetLightPosition(glm::vec3(0, 0, 1));

    Animations.reset();

    // Close the screen we're loading if we asked to interrupt its loading.
    if (ThreadInterrupted)
        Next->Close();
	
    ChangeState(StateRunning);
}

bool ScreenLoading::Run(double TimeDelta)
{
    if (!LoadThread && !ThreadInterrupted)
        return (Running = RunNested(TimeDelta));

    if (!Animations) return false;

    Animations->DrawTargets(TimeDelta);

    if (FinishedLoading)
    {
        LoadThread->join();
        LoadThread = nullptr;
        Next->InitializeResources();
        ChangeState(StateExit);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(16));
    return Running;
}

bool ScreenLoading::HandleInput(int32_t key, bool isPressed, bool isMouseInput)
{
    if (!LoadThread)
    {
        if (Next)
            return Next->HandleInput(key, isPressed, isMouseInput);
        return true;
    }

    if (!isPressed)
    {
        if (BindingsManager::TranslateKey(key) == KT_Escape)
        {
            Next->RequestInterrupt();
            ThreadInterrupted = true;
        }
    }

    return true;
}

bool ScreenLoading::HandleScrollInput(double xOff, double yOff)
{
    if (!LoadThread)
    {
        return Next->HandleScrollInput(xOff, yOff);
    }

    return Screen::HandleScrollInput(xOff, yOff);
}

void ScreenLoading::Cleanup()
{
}