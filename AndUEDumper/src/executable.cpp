
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <unordered_map>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#include "Utils/KittyCmdln.hpp"
#include "Utils/Logger.hpp"
#include "Utils/ProgressUtils.hpp"

#include "Dumper.hpp"

#include "UE/UEMemory.hpp"
#include "UE/UEGameProfile.hpp"

#include "UE/UEGameProfiles/ArenaBreakout.hpp"
#include "UE/UEGameProfiles/BlackClover.hpp"
#include "UE/UEGameProfiles/Dislyte.hpp"
#include "UE/UEGameProfiles/Farlight.hpp"
#include "UE/UEGameProfiles/MortalKombat.hpp"
#include "UE/UEGameProfiles/PES.hpp"
#include "UE/UEGameProfiles/Torchlight.hpp"
#include "UE/UEGameProfiles/WutheringWaves.hpp"
#include "UE/UEGameProfiles/RealBoxing2.hpp"
#include "UE/UEGameProfiles/OdinValhalla.hpp"
#include "UE/UEGameProfiles/Injustice2.hpp"
#include "UE/UEGameProfiles/DeltaForce.hpp"
#include "UE/UEGameProfiles/RooftopsParkour.hpp"
#include "UE/UEGameProfiles/BabyYellow.hpp"
#include "UE/UEGameProfiles/TowerFantasy.hpp"
#include "UE/UEGameProfiles/BladeSoul.hpp"
#include "UE/UEGameProfiles/Lineage2.hpp"
#include "UE/UEGameProfiles/NightCrows.hpp"
#include "UE/UEGameProfiles/Case2.hpp"
#include "UE/UEGameProfiles/KingArthur.hpp"
#include "UE/UEGameProfiles/Century.hpp"
#include "UE/UEGameProfiles/HelloNeighbor.hpp"
#include "UE/UEGameProfiles/HelloNeighborND.hpp"
#include "UE/UEGameProfiles/SFG2.hpp"
#include "UE/UEGameProfiles/ArkUltimate.hpp"
#include "UE/UEGameProfiles/Auroria.hpp"
#include "UE/UEGameProfiles/LineageW.hpp"
#include "UE/UEGameProfiles/RLSideswipe.hpp"
#include "UE/UEGameProfiles/PUBG.hpp"
#include "UE/UEGameProfiles/SFG3.hpp"

std::vector<IGameProfile*> UE_Games = {
    new PESProfile(),
    new DislyteProfile(),
    new MortalKombatProfile(),
    new FarlightProfile(),
    new TorchlightProfile(),
    new ArenaBreakoutProfile(),
    new BlackCloverProfile(),
    new WutheringWavesProfile(),
    new RealBoxing2Profile(),
    new OdinValhallaProfile(),
    new Injustice2Profile(),
    new DeltaForceProfile(),
    new RooftopParkourProfile(),
    new BabyYellowProfile(),
    new TowerFantasyProfile(),
    new BladeSoulProfile(),
    new Lineage2Profile(),
    new Case2Profile(),
    new CenturyProfile(),
    new KingArthurProfile(),
    new NightCrowsProfile(),
    new HelloNeighborProfile(),
    new HelloNeighborNDProfile(),
    new SFG2Profile(),
    new ArkUltimateProfile(),
    new AuroriaProfile(),
    new LineageWProfile(),
    new RLSideswipeProfile(),
    new PUBGProfile(),
    new SFG3Profile(),
};

#define kUEDUMPER_VERSION "4.3.0"

bool bNeededHelp = false;

int main(int argc, char** args)
{
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);
    setbuf(stdin, nullptr);

    LOGI("Using UE Dumper %s", kUEDUMPER_VERSION);

    KittyCmdln cmdline(argc, args);

    cmdline.setUsage("Usage: ./UEDump3r [-h] [-o] [ options ]");

    cmdline.addCmd("-h", "--help", "show available arguments", false, [&cmdline]()
    { std::cout << cmdline.toString() << std::endl; bNeededHelp = true; });

    char sOutDir[0xff] = {0};
    cmdline.addScanf("-o", "--output", "specify output directory path.", true, "%s", sOutDir);

    // optional
    char sGamePkg[0xff] = {0};
    cmdline.addScanf("-p", "--package", "specify game package ID in advance.", false, "%s", sGamePkg);

    // options
    bool bDumpLib = false;
    cmdline.addFlag("-d", "--dumplib", "dump UE library from memory.", false, &bDumpLib);

    cmdline.parseArgs();

    if (bNeededHelp)
    {
        return 0;
    }

    if (!cmdline.requiredCmdsCheck())
    {
        LOGE("Required arguments needed. see -h.");
        return 1;
    }

    std::string sOutDirectory = sOutDir, sGamePackage = sGamePkg;
    if (sOutDirectory.empty())
    {
        LOGE("Output directory path is not specified.");
        return 1;
    }

    if (sGamePackage.empty())
    {
        std::sort(UE_Games.begin(), UE_Games.end(), [](const IGameProfile* a, const IGameProfile* b)
        {
            return a->GetAppName() < b->GetAppName();
        });

        std::cout << "Choose from the available games:" << std::endl;
        int gameIndex = 1;
        std::map<int, std::pair<int, int>> gameIndexMap;
        for (size_t i = 0; i < UE_Games.size(); i++)
        {
            const auto& appIDs = UE_Games[i]->GetAppIDs();
            for (size_t j = 0; j < appIDs.size(); j++)
            {
                const char* nspace = gameIndex < 10 ? "  " : " ";
                std::cout << "\t" << gameIndex << nspace << ": " << UE_Games[i]->GetAppName() << " | " << appIDs[j].c_str() << std::endl;
                gameIndexMap[gameIndex] = {i, j};
                gameIndex++;
            }
        }

        std::cout << "Game number: ";
        int gameNumber = 0;
        scanf("%d", &gameNumber);
        if (gameIndexMap.count(gameNumber) <= 0)
        {
            LOGE("Game number is not available.");
            return 1;
        }

        sGamePackage = UE_Games[gameIndexMap[gameNumber].first]->GetAppIDs()[gameIndexMap[gameNumber].second];
    }

    auto gamePIDs = KittyMemoryEx::getProcessIDs(sGamePackage);
    if (gamePIDs.empty())
    {
        LOGE("Couldn't find \"%s\" in the running processes list.", sGamePackage.c_str());
        return 1;
    }

    pid_t gamePID = 0;
    if (gamePIDs.size() > 1)
    {
        std::cout << "Found multiple processes with same package name, Choose one: " << std::endl;
        for (size_t i = 0; i < gamePIDs.size(); i++)
        {
            std::cout << "\t" << i << ": PID(" << gamePIDs[i] << ")" << std::endl;
        }
        std::cout << "Process number: ";
        int gamePidIndex = 0;
        scanf("%d", &gamePidIndex);
        if (gamePidIndex < 0 || gamePidIndex >= int(gamePIDs.size()))
        {
            LOGE("Selected PID index is out of range.");
            return 1;
        }
        gamePID = gamePIDs[gamePidIndex];
    }
    else
    {
        gamePID = gamePIDs.front();
    }

    LOGI("Game: %s", sGamePackage.c_str());
    LOGI("Process ID: %d", gamePID);
    LOGI("Output directory: %s", sOutDirectory.c_str());
    LOGI("Dump Library: %s", bDumpLib ? "true" : "false");
    LOGI("==========================");

    std::string sDumpDir = sOutDirectory + "/UEDump3r";
    std::string sDumpGameDir = sDumpDir + "/" + sGamePackage;
    IOUtils::delete_directory(sDumpGameDir);

    if (IOUtils::mkdir_recursive(sDumpGameDir, 0777) == -1)
    {
        int err = errno;
        LOGE("Couldn't create Output Directory [\"%s\"] error=%d | %s.", sDumpDir.c_str(), err, strerror(err));
        return 1;
    }

    LOGI("Initializing Memory...");
    if (!kMgr.initialize(gamePID, EK_MEM_OP_SYSCALL, false) && !kMgr.initialize(gamePID, EK_MEM_OP_IO, false))
    {
        LOGE("Failed to initialize KittyMemoryMgr.");
        return 1;
    }

    UEDumper uEDumper{};

    uEDumper.setDumpExeInfoNotify([](bool bFinished)
    {
        if (!bFinished)
        {
            LOGI("Dumping Executable Info...");
        }
    });

    uEDumper.setDumpNamesInfoNotify([](bool bFinished)
    {
        if (!bFinished)
        {
            LOGI("Dumping Names Info...");
        }
    });

    uEDumper.setDumpObjectsInfoNotify([](bool bFinished)
    {
        if (!bFinished)
        {
            LOGI("Dumping Objects Info...");
        }
    });

    uEDumper.setDumpOffsetsInfoNotify([](bool bFinished)
    {
        if (!bFinished)
        {
            LOGI("Dumping Offsets Info...");
        }
    });

    uEDumper.setObjectsProgressCallback([](const SimpleProgressBar& progress)
    {
        static bool once = false;
        if (!once)
        {
            once = true;
            LOGI("Gathering UObjects....");
        };
        static int lastPercent = -1;
        int currPercent = progress.getPercentage();
        if (lastPercent != currPercent)
        {
            lastPercent = currPercent;
            progress.print();
            if (progress.isComplete())
                std::cout << "\n";
        }
    });

    uEDumper.setDumpProgressCallback([](const SimpleProgressBar& progress)
    {
        static bool once = false;
        if (!once)
        {
            once = true;
            LOGI("Dumping....");
        };
        static int lastPercent = -1;
        int currPercent = progress.getPercentage();
        if (lastPercent != currPercent)
        {
            lastPercent = currPercent;
            progress.print();
            if (progress.isComplete())
                std::cout << "\n";
        }
    });

    bool dumpSuccess = false;
    std::unordered_map<std::string, BufferFmt> dumpbuffersMap;
    auto dmpStart = std::chrono::steady_clock::now();

    for (auto& it : UE_Games)
    {
        for (auto& pkg : it->GetAppIDs())
        {
            if (sGamePackage != pkg)
                continue;

            if (bDumpLib)
            {
                auto ue_elf = it->GetUnrealELF();
                if (!ue_elf.isValid())
                {
                    LOGE("Couldn't find a valid UE ELF in target process maps.");
                    return 1;
                }

                LOGI("Dumping unreal lib from memory...");
                std::string libDumpPath = KittyUtils::String::Fmt("%s/libUE_%p-%p.so", sDumpGameDir.c_str(), ue_elf.base(), ue_elf.end());
                bool res = kMgr.dumpMemELF(ue_elf, libDumpPath);
                LOGI("Dumping lib: %s.", res ? "success" : "failed");
                if (res)
                {
                    LOGI("%s", libDumpPath.c_str());
                }
                LOGI("==========================");
            }

            LOGI("Initializing Dumper...");
            if (uEDumper.Init(it))
            {
                dumpSuccess = uEDumper.Dump(&dumpbuffersMap);
            }

            goto done;
        }
    }

done:

    if (!dumpSuccess && uEDumper.GetLastError().empty())
    {
        LOGE("Game is not supported. check AppID.");
        return 1;
    }

    if (dumpbuffersMap.empty())
    {
        LOGE("Dump Failed, Error <Buffers empty>");
        LOGE("Dump Status <%s>", uEDumper.GetLastError().c_str());
        return 1;
    }

    LOGI("Saving Files...");

    for (const auto& it : dumpbuffersMap)
    {
        if (!it.first.empty())
        {
            std::string path = KittyUtils::String::Fmt("%s/%s", sDumpGameDir.c_str(), it.first.c_str());
            it.second.writeBufferToFile(path);
        }
    }

    auto dmpEnd = std::chrono::steady_clock::now();
    std::chrono::duration<float, std::milli> dmpDurationMS = (dmpEnd - dmpStart);

    if (!uEDumper.GetLastError().empty())
    {
        LOGI("Dump Status: %s", uEDumper.GetLastError().c_str());
    }
    LOGI("Dump Duration: %.2fms", dmpDurationMS.count());
    LOGI("Dump Location: %s", sDumpGameDir.c_str());

    return 0;
}