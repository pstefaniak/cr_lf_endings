#include "CGame.h"
#include "CGameOptions.h"
#include "IFrameListener.h"
#include "IKeyListener.h"
#include "IMouseListener.h"
#include "IJoyListener.h"
#include "Console/CConsole.h"
#include "Logic/CLogic.h"
#include "Utils/CClock.h"
#include "Utils/CRand.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"
#include "Input/CInputHandler.h"
#include "Input/CBindManager.h"
#include "Commands/CCommands.h"
#include "Rendering/CCamera.h"
#include "Rendering/CDrawableManager.h"
#include "CTimeManipulator.h"
#include "GUI/CRoot.h"
#include "GUI/CTextArea.h"
#include "Audio/CAudioManager.h"
#include "Logic/Quests/CQuestManager.h"
#include <sstream>
#ifdef PLATFORM_MACOSX
#include <gl.h>
#else
#include <GL/gl.h>
#endif
#include "Utils/HRTimer.h"
#include "Map/CMapManager.h"

#include "VFS/vfs.h"

#include <SFML/Graphics/RenderWindow.hpp>

#ifdef __I_AM_TOXIC__
#include "Logic/CPhysicalManager.h"
#include "Logic/CPhysical.h"

#include "Utils/ToxicUtils.h"
#endif

template<> CGame* CSingleton<CGame>::msSingleton = 0;

bool CGame::dontLoadWindowHack = false;

CGame::CGame():
    mRenderWindow(0),
    mPaused(false),
    mConsoleOpened(false),
    mFreezed(false),
    mWasFreezedBeforeLostFocus(false),
    mShowFps(false),
    mTimeAccumulator(0.f),
    mTimeStep(0.025f),
	mLoading(false),
    mLoaded(false),
	mLoadingFrameCount(0),
	mLoadingData(NULL),
	mMapToLoadAtInit(NULL)
{
    fprintf( stderr, "CGame::CGame()\n" );
    fflush(stderr);

#ifndef __EDITOR__
	
	/*
	Na MacOSX byl blad Freetype, ktory nie moze tworzyc (renderowac) fontow jezeli nie istnieje okno (kontekst).
	Tymczasem singletony inicjalizuja sie tutaj jak szalone i probuja m. in. tworzyc fonty przed stworzeniem okna.
	 
	27.07.2010 -- Dab */
	
    // cokolwiek, byle nie full-screen
	if (!dontLoadWindowHack){

		sf::VideoMode currentVideoMode = sf::VideoMode::GetDesktopMode();

		mRenderWindow = new sf::RenderWindow(sf::VideoMode(800, 600), "", sf::Style::None);

		StringUtils::InitializeValidChars();
		gGameOptions.LoadOptions();
		gGameOptions.UpdateWindow(); // tworzy to wlasciwe okno
		
		assert( mRenderWindow && "CGame::CGame(): unable to create RenderWindow\n" );
	}
	
    fflush(stderr);

#endif
}

CGame::~CGame()
{
    fprintf( stderr, "CGame::~CGame()\n" );
    Cleanup();
}

void CGame::Cleanup(){
    fprintf( stderr, "CGame::Cleanup()\n" );

#ifndef __EDITOR__
    if (mRenderWindow)
        delete mRenderWindow;
#endif

    mRenderWindow = NULL;
}

void CGame::Run()
{
    // bezpieczna, reczna inicjalizacja niektorych singletonow
    gConsole;
    gInputHandler;
    gLogic;
    gCamera;
    gAudioManager;
	gQuestManager;

#ifdef __I_AM_TOXIC__
    ToxicUtils::Editor editor;
    AddKeyListener( &editor ); 
    AddMouseListener( &editor ); 
#endif

    gRand.Seed((unsigned) gClock.GetTime());

	if (mMapToLoadAtInit != NULL){
		gLogic.StartNewGame(L"startup-no-map");
		gCommands.ParseCommand(std::wstring(L"load-map ") + StringUtils::ConvertToWString(*mMapToLoadAtInit));
	} else {
		gCommands.ParseCommand(L"exec startup");
	}
    gTimeManipulator.SetMaxDt(0.1f);

    glClearColor(0.f, 0.f, 0.f, 0.f);

    // dex: porownanie wydajnosci glClear i mRenderWindow->Clear, mozna sobie sprawdzic..
    /*
    HRTimer timer;
    timer.StartTimer();
    for (int i = 0; i < 10000; ++i, glClear(GL_COLOR_BUFFER_ssssssssssssBIT));
    double time = timer.StopTimer();
    fprintf(stderr, "TEST: 10000 glClears: %f\n", time);
    timer.StartTimer();
    for (int i = 0; i < 10000; ++i, mRenderWindow->Clear(sf::Color::Black));
    time = timer.StopTimer();
    fprintf(stderr, "TEST: 10000 mRenderWindow->Clears: %f\n", time);
    */

    while ( mRenderWindow->IsOpened() )
    {
		MainLoopStep();
    }
}

sf::RenderWindow* CGame::GetRenderWindow()
{
    return mRenderWindow;
}

void CGame::SetRenderWindow(sf::RenderWindow *renderWindow)
{
    mRenderWindow = renderWindow;
    mRenderWindow->ShowMouseCursor(false);
}

void CGame::AddFrameListener( IFrameListener* frameListener )
{
    mFrameListeners.insert( frameListener );
}

void CGame::AddKeyListener( IKeyListener* keyListener )
{
    mKeyListeners.insert( keyListener );
}

void CGame::AddMouseListener( IMouseListener* mouseListener )
{
	mMouseListeners.insert( mouseListener );
}

void CGame::AddJoyListener( IJoyListener* joyListener )
{
    mJoyListeners.insert( joyListener );
}   

void CGame::MainLoopStep()
{	
	if (mLoading){
		mFreezed = true;
		mLoadingFrameCount++;
		if (mLoadingFrameCount >= 3){
            if (!mLoaded)
            {
                // tlo sie zmienilo, laduj
			    mLoadingRoutine(mLoadingData);
                mLoaded = true;
            }
		} else {
			gLogic.ShowLoading(true);
		}
	}

    float secondsPassed = gTimeManipulator( mRenderWindow->GetFrameTime() );
    //src/CGame.cpp:172: warning: unused variable 'lastElapsed'
	//float lastElapsed = gClock.GetElapsedTime();
    mTimeAccumulator += secondsPassed;

    while (mTimeAccumulator > mTimeStep)
    {
        // a to tutaj, zeby ekran loading nie zapier#$%^ jak glupi
		if (mLoading && mLoadingFrameCount >= 3 && mLoaded)
        {
            // podczas chowania gra ma chodzic
            mFreezed = false;

            if (!gLogic.ShowLoading(false, false))
            {
                // animacja sie skonczyla, czysc
		        mLoading = false;
                mLoaded = false;
		        mLoadingFrameCount = 0;

                // screen
                gCommands.ParseCommand(L"capture-screen");
                gLogic.SaveGame(gGameOptions.GetUserDir() + "/game.save", true);
            }
        }

        std::set< IFrameListener* >::iterator i;
        if ((!mPaused) && (!mConsoleOpened) && (!mFreezed)) {
            for (i = mFrameListeners.begin(); i != mFrameListeners.end(); i++) {
                ;(*i)->FrameStarted( mTimeStep );
            }
        } else {
            EPauseVariant pv;
            if (mPaused)
            {
                if (gLogic.GetState() == L"pause-menu")
                    pv = pvPauseMenu;
                else
                    pv = pvTotal;
            }
            else if (mConsoleOpened)
                pv = pvConsoleInduces;
            else if (mLoading)
                pv = pvLoading;
            else
                pv = pvLogicOnly;
            for (i = mFrameListeners.begin(); i != mFrameListeners.end(); i++) {
                if ((*i)->FramesDuringPause(pv))
                    (*i)->FrameStarted( mTimeStep );
            }
        }

        sf::Event event;
        while ( mRenderWindow->GetEvent( event ))
        {
            switch ( event.Type )
            {
            case sf::Event::Closed:
                mRenderWindow->Close();
                return;
            case sf::Event::KeyPressed:
                for ( std::set< IKeyListener* >::iterator i = mKeyListeners.begin() ; i != mKeyListeners.end(); i++ )
                    (*i)->KeyPressed( event.Key );
                break;
            case sf::Event::KeyReleased:
                for ( std::set< IKeyListener* >::iterator i = mKeyListeners.begin() ; i != mKeyListeners.end(); i++ )
                    (*i)->KeyReleased( event.Key );
                break;
		    case sf::Event::MouseButtonPressed:
			    for ( std::set< IMouseListener* >::iterator i = mMouseListeners.begin() ; i != mMouseListeners.end(); i++ )
				    (*i)->MousePressed( event.MouseButton );
			    break;
		    case sf::Event::MouseButtonReleased:
			    for ( std::set< IMouseListener* >::iterator i = mMouseListeners.begin() ; i != mMouseListeners.end(); i++ )
				    (*i)->MouseReleased( event.MouseButton );
			    break;
		    case sf::Event::MouseMoved:
			    for ( std::set< IMouseListener* >::iterator i = mMouseListeners.begin() ; i != mMouseListeners.end(); i++ )
				    (*i)->MouseMoved( event.MouseMove );
			    break;
            case sf::Event::MouseWheelMoved:
			    for ( std::set< IMouseListener* >::iterator i = mMouseListeners.begin() ; i != mMouseListeners.end(); i++ )
				    (*i)->MouseWheelMoved( event.MouseWheel );
			    break;
			case sf::Event::JoyButtonPressed:
                for ( std::set< IJoyListener* >::iterator i = mJoyListeners.begin() ; i != mJoyListeners.end(); i++ )
                    (*i)->JoyButtonPressed( event.JoyButton );
                break;
            case sf::Event::JoyButtonReleased:
                for ( std::set< IJoyListener* >::iterator i = mJoyListeners.begin() ; i != mJoyListeners.end(); i++ )
                    (*i)->JoyButtonReleased( event.JoyButton );
                break;
			case sf::Event::JoyMoved:
			    for ( std::set< IJoyListener* >::iterator i = mJoyListeners.begin() ; i != mJoyListeners.end(); i++ )
				    (*i)->JoyMoved( event.JoyMove );
			    break;
            case sf::Event::LostFocus:
                mWasFreezedBeforeLostFocus = mFreezed;
                mFreezed = true;
                System::Input::CBindManager::ReleaseKeys();
                break;
            case sf::Event::GainedFocus:
                mFreezed = mWasFreezedBeforeLostFocus;
                break;
            default:
                break;
            }
        }
    	
/*        if ( mRenderWindow->GetInput().IsKeyDown( sf::Key::F10 ) )
        {
            mRenderWindow->Close();
            return;
        }*/
        
        mTimeAccumulator -= mTimeStep;
    }

    static float lastFPSDisplay = -1.0f;
    float cTime = gClock.GetTotalTime();
    if (mShowFps)
        if (cTime > lastFPSDisplay + 1.0f){
            std::wstringstream s;
            s << "FPS: " << gClock.GetFPS() << "\nAverageFPS: " << gClock.GetAverageFPS() << "\nCurrentMap: " << gMapManager.GetLevel();

#ifdef __I_AM_TOXIC__
            CPhysical * player = gPhysicalManager.GetPhysicalById( L"player0" );
            if ( player ) {
                s << "\n" << player->GetPosition().x << " " << player->GetPosition().y;
            }
#endif

            ((GUI::CTextArea*) gGUI.FindObject("fpstext"))->SetText(s.str());
            lastFPSDisplay = cTime;
        }
	
#ifndef __EDITOR__
    glClear(GL_COLOR_BUFFER_BIT);

    gDrawableManager.DrawFrame();
    mRenderWindow->Display();
#endif

    // uaktualnienie zegara
    gClock.Update();
}

void CGame::SetShowingFps(bool show)
{
    mShowFps = show;
    if (GUI::CGUIObject* fps = gGUI.FindObject("fpstext"))
        fps->SetVisible(show);
}

#ifdef __EDITOR__
void CGame::Init(sf::RenderWindow* wnd)
    {
        gGame;

        gGame.mRenderWindow = wnd;
        wnd->UseVerticalSync(true);
        gGameOptions.LoadOptions();
        gGameOptions.UpdateWindow();

        gGame.mFreezed = true;
        gGame.mWasFreezedBeforeLostFocus = true;

        gConsole;
        gInputHandler;
        gLogic;
        gCamera;
        gAudioManager;
        gDropManager;
	    gQuestManager;

        gRand.Seed((unsigned) gClock.GetTime());

        gAudioManager.GetMusicPlayer().SetMusicVolume(0.f);
        gAudioManager.GetSoundPlayer().SetGeneralVolume(0.f);
        gCommands.ParseCommand(L"exec startup");
        gTimeManipulator.SetMaxDt(0.1f);

        gLogic.StartEditor();

        glClearColor(0.f, 0.f, 0.5f, 0.f);
    }
#endif

void CGame::ScheduleLoadingRoutine(CGame::loadingRoutine method, void *data, bool hideLoadingScreen){
	mLoadingRoutine = method;
	mLoadingData = data;
	mLoading = true;
    mLoadingFrameCount = (hideLoadingScreen ? 3 : 0);
}
