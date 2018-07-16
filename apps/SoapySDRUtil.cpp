// Copyright (c) 2014-2018 Josh Blum
// SPDX-License-Identifier: BSL-1.0

#include <SoapySDR/Version.hpp>
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/ConverterRegistry.hpp>
#include <algorithm> //sort, min, max
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <iomanip>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>

std::string SoapySDRDeviceProbe(SoapySDR::Device *);
int SoapySDRRateTest(
    const std::string &argStr,
    const double sampleRate,
    const std::string &channelStr,
    const std::string &directionStr);

/***********************************************************************
 * Print the banner
 **********************************************************************/
static void printBanner(void)
{
    std::cerr <<
        "######################################################\n"
        "##     Soapy SDR -- the SDR abstraction library     ##\n"
        "######################################################\n"
        "\n";
}

/***********************************************************************
 * Print help message
 **********************************************************************/
static int printHelp(void)
{
    std::cerr <<
        "Usage SoapySDRUtil [options]\n"
        "  Options summary:\n"
        "    --help \t\t\t\t Print this help message\n"
        "    --info \t\t\t\t Print module information\n"
        "    --find[=\"driver=foo,type=bar\"] \t Discover available devices\n"
        "    --make[=\"driver=foo,type=bar\"] \t Create a device instance\n"
        "    --probe[=\"driver=foo,type=bar\"] \t Print detailed information\n"
        "\n"
        "  Advanced options:\n"
        "    --check[=driverName] \t\t Check if driver is present\n"
        "    --sparse             \t\t Simplified output for --find\n"
        "\n"
        "  Rate testing options:\n"
        "    --args[=\"driver=foo\"] \t\t Arguments for testing\n"
        "    --rate[=stream rate Sps] \t\t Rate in samples per second\n"
        "    --channels[=\"0, 1, 2\"] \t\t List of channels, default 0\n"
        "    --direction[=RX or TX] \t\t Specify the channel direction\n"
        "\n";
    return EXIT_SUCCESS;
}

/***********************************************************************
 * Print version and module info
 **********************************************************************/
static int printInfo(void)
{
    std::cout << "Lib Version: v" << SoapySDR::getLibVersion() << std::endl;
    std::cout << "API Version: v" << SoapySDR::getAPIVersion() << std::endl;
    std::cout << "ABI Version: v" << SoapySDR::getABIVersion() << std::endl;
    std::cout << "Install root: " << SoapySDR::getRootPath() << std::endl;

    //max path length for alignment
    size_t maxPathLen(0);
    const auto searchPaths = SoapySDR::listSearchPaths();
    const auto modules = SoapySDR::listModules();
    for (const auto &path : searchPaths) maxPathLen = std::max(maxPathLen, path.size());
    for (const auto &mod : modules) maxPathLen = std::max(maxPathLen, mod.size());

    //print search path information
    for (const auto &path : searchPaths)
    {
        struct stat info;
        const bool missing = (stat(path.c_str(), &info) != 0);
        std::cout << "Search path:  " << path;
        if (missing) std::cout << std::string(maxPathLen-path.size(), ' ') << " (missing)";
        std::cout << std::endl;
    }

    //load each module and print information
    for (const auto &mod : modules)
    {
        std::cout << "Module found: " << mod;
        const auto &errMsg = SoapySDR::loadModule(mod);
        if (not errMsg.empty()) std::cout << "\n  " << errMsg;
        const auto version = SoapySDR::getModuleVersion(mod);
        if (not version.empty()) std::cout << std::string(maxPathLen-mod.size(), ' ') << " (" << version << ")";
        std::cout << std::endl;
    }
    if (modules.empty()) std::cout << "No modules found!" << std::endl;

    std::cout << "Available factories... ";
    std::string factories;
    for (const auto &it : SoapySDR::Registry::listFindFunctions())
    {
        if (not factories.empty()) factories += ", ";
        factories += it.first;
    }
    if (factories.empty()) factories = "No factories found!";
    std::cout << factories << std::endl;

    std::cout << "Available converters..." << std::endl;
    for (const auto &source : SoapySDR::ConverterRegistry::listAvailableSourceFormats())
    {
        std::string targets;
        for (const auto &target : SoapySDR::ConverterRegistry::listTargetFormats(source))
        {
            if (not targets.empty()) targets += ", ";
            targets += target;
        }
        std::cout << " - " << std::setw(5) << source << " -> [" << targets << "]" << std::endl;
    }

    return EXIT_SUCCESS;
}

/***********************************************************************
 * Find devices and print args
 **********************************************************************/
static int findDevices(const std::string &argStr, const bool sparse)
{
    const auto results = SoapySDR::Device::enumerate(argStr);
    if (sparse)
    {
        std::vector<std::string> sparseResults;
        for (size_t i = 0; i < results.size(); i++)
        {
            const auto it = results[i].find("label");
            if (it != results[i].end()) sparseResults.push_back(it->second);
            else sparseResults.push_back(SoapySDR::KwargsToString(results[i]));
        }
        std::sort(sparseResults.begin(), sparseResults.end());
        for (size_t i = 0; i < sparseResults.size(); i++)
        {
            std::cout << i << ": " << sparseResults[i] << std::endl;
        }
    }
    else
    {
        for (size_t i = 0; i < results.size(); i++)
        {
            std::cout << "Found device " << i << std::endl;
            for (const auto &it : results[i])
            {
                std::cout << "  " << it.first << " = " << it.second << std::endl;
            }
            std::cout << std::endl;
        }
        if (results.empty()) std::cerr << "No devices found!" << std::endl;
        else std::cout << std::endl;
    }
    return results.empty()?EXIT_FAILURE:EXIT_SUCCESS;
}

/***********************************************************************
 * Make device and print hardware info
 **********************************************************************/
static int makeDevice(const std::string &argStr)
{
    std::cout << "Make device " << argStr << std::endl;
    try
    {
        auto device = SoapySDR::Device::make(argStr);
        std::cout << "  driver=" << device->getDriverKey() << std::endl;
        std::cout << "  hardware=" << device->getHardwareKey() << std::endl;
        for (const auto &it : device->getHardwareInfo())
        {
            std::cout << "  " << it.first << "=" << it.second << std::endl;
        }
        SoapySDR::Device::unmake(device);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error making device: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << std::endl;
    return EXIT_SUCCESS;
}

/***********************************************************************
 * Make device and print detailed info
 **********************************************************************/
static int probeDevice(const std::string &argStr)
{
    std::cout << "Probe device " << argStr << std::endl;
    try
    {
        auto device = SoapySDR::Device::make(argStr);
        std::cout << SoapySDRDeviceProbe(device) << std::endl;
        SoapySDR::Device::unmake(device);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error probing device: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << std::endl;
    return EXIT_SUCCESS;
}

/***********************************************************************
 * Check the registry for a specific driver
 **********************************************************************/
static int checkDriver(const std::string &driverName)
{
    std::cout << "Loading modules... " << std::flush;
    SoapySDR::loadModules();
    std::cout << "done" << std::endl;

    std::cout << "Checking driver '" << driverName << "'... " << std::flush;
    const auto factories = SoapySDR::Registry::listFindFunctions();

    if (factories.find(driverName) == factories.end())
    {
        std::cout << "MISSING!" << std::endl;
        return EXIT_FAILURE;
    }
    else
    {
        std::cout << "PRESENT" << std::endl;
        return EXIT_SUCCESS;
    }
}

/***********************************************************************
 * main utility entry point
 **********************************************************************/
int main(int argc, char *argv[])
{
    std::string argStr;
    std::string chanStr;
    std::string dirStr;
    double sampleRate(0.0);
    std::string driverName;
    bool findDevicesFlag(false);
    bool sparsePrintFlag(false);
    bool makeDeviceFlag(false);
    bool probeDeviceFlag(false);

    /*******************************************************************
     * parse command line options
     ******************************************************************/
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"find", optional_argument, 0, 'f'},
        {"make", optional_argument, 0, 'm'},
        {"info", optional_argument, 0, 'i'},
        {"probe", optional_argument, 0, 'p'},

        {"check", optional_argument, 0, 'c'},
        {"sparse", no_argument, 0, 's'},

        {"args", optional_argument, 0, 'a'},
        {"rate", optional_argument, 0, 'r'},
        {"channels", optional_argument, 0, 'n'},
        {"direction", optional_argument, 0, 'd'},
        {0, 0, 0,  0}
    };
    int long_index = 0;
    int option = 0;
    while ((option = getopt_long_only(argc, argv, "", long_options, &long_index)) != -1)
    {
        switch (option)
        {
        case 'h':
            printBanner();
            return printHelp();
        case 'i':
            printBanner();
            return printInfo();
        case 'f':
            findDevicesFlag = true;
            if (optarg != nullptr) argStr = optarg;
            break;
        case 'm':
            makeDeviceFlag = true;
            if (optarg != nullptr) argStr = optarg;
            break;
        case 'p':
            probeDeviceFlag = true;
            if (optarg != nullptr) argStr = optarg;
            break;
        case 'c':
            if (optarg != nullptr) driverName = optarg;
            break;
        case 's':
            sparsePrintFlag = true;
            break;
        case 'a':
            if (optarg != nullptr) argStr = optarg;
            break;
        case 'r':
            if (optarg != nullptr) sampleRate = std::stod(optarg);
            break;
        case 'n':
            if (optarg != nullptr) chanStr = optarg;
            break;
        case 'd':
            if (optarg != nullptr) dirStr = optarg;
            break;
        }
    }

    if (not sparsePrintFlag) printBanner();
    if (not driverName.empty()) return checkDriver(driverName);
    if (findDevicesFlag) return findDevices(argStr, sparsePrintFlag);
    if (makeDeviceFlag)  return makeDevice(argStr);
    if (probeDeviceFlag) return probeDevice(argStr);

    //invoke utilities that rely on multiple arguments
    if (sampleRate != 0.0)
    {
        return SoapySDRRateTest(argStr, sampleRate, chanStr, dirStr);
    }

    //unknown or unspecified options, do help...
    return printHelp();
}
