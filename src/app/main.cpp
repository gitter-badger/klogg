/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <memory>

#include <cli11/cli11.hpp>

#ifdef KLOGG_OVERRIDE_MALLOC
#include <tbb/tbbmalloc_proxy.h>
#endif

#include "kloggapp.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // _WIN32

#include "configuration.h"
#include "klogg_version.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "styles.h"
#include "versionchecker.h"

#include <QFileInfo>
#include <QStyleFactory>
#include <QtCore/QJsonDocument>

#ifdef KLOGG_PORTABLE
const bool PersistentInfo::ForcePortable = true;
#else
const bool PersistentInfo::ForcePortable = false;
#endif

static void print_version();

void setApplicationAttributes()
{
    // When QNetworkAccessManager is instantiated it regularly starts polling
    // all network interfaces to see if anything changes and if so, what. This
    // creates a latency spike every 10 seconds on Mac OS 10.12+ and Windows 7 >=
    // when on a wifi connection.
    // So here we disable it for lack of better measure.
    // This will also cause this message: QObject::startTimer: Timers cannot
    // have negative intervals
    // For more info see:
    // - https://bugreports.qt.io/browse/QTBUG-40332
    // - https://bugreports.qt.io/browse/QTBUG-46015
    qputenv( "QT_BEARER_POLL_TIMEOUT", QByteArray::number( std::numeric_limits<int>::max() ) );

    const auto& config = Configuration::getSynced();

    if ( config.enableQtHighDpi() ) {
        // This attribute must be set before QGuiApplication is constructed:
        QCoreApplication::setAttribute( Qt::AA_EnableHighDpiScaling );
        // We support high-dpi (aka Retina) displays
        QCoreApplication::setAttribute( Qt::AA_UseHighDpiPixmaps );

#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            static_cast<Qt::HighDpiScaleFactorRoundingPolicy>( config.scaleFactorRounding() ) );
#endif
    }
    else {
        QCoreApplication::setAttribute( Qt::AA_DisableHighDpiScaling );
    }

    QCoreApplication::setAttribute( Qt::AA_DontShowIconsInMenus );

    if ( config.enableQtHighDpi() ) {
    }

    // fractional scaling

#ifdef Q_OS_WIN
    QCoreApplication::setAttribute( Qt::AA_DisableWindowContextHelpButton );

#endif
}

struct CliParameters {
    bool new_session = false;
    bool load_session = false;
    bool multi_instance = false;
    bool log_to_file = false;
    bool follow_file = false;
    int64_t log_level = static_cast<int64_t>( plog::warning );

    std::vector<QString> filenames;

    int window_width = 0;
    int window_height = 0;

    CliParameters() = default;

    CliParameters( CLI::App& options, int argc, char* const* argv )
    {
        options.add_flag_function(
            "-v,--version",
            []( auto ) {
                print_version();
                exit( 0 );
            },
            "print version information" );

        options.add_flag(
            "-m,--multi", multi_instance,
            "allow multiple instance of klogg to run simultaneously (use together with -s)" );
        options.add_flag( "-s,--load-session", load_session,
                          "load the previous session (default when no file is passed)" );
        options.add_flag( "-n,--new-session", new_session,
                          "do not load the previous session (default when a file is passed)" );

        options.add_flag( "-l,--log", log_to_file, "save the log to a file" );

        options.add_flag( "-f,--follow", follow_file, "follow initial opened files" );

        options.add_flag_function(
            "-d,--debug",
            [ this ]( auto count ) { log_level = static_cast<int64_t>( plog::warning ) + count; },
            "output more debug (include multiple times for more verbosity e.g. -dddd)" );

        options.add_flag( "--window-width", window_width, "new window width" )
            ->needs( options.add_flag( "--window-height", window_height, "new window height" ) );

        std::vector<std::string> raw_filenames;
        options.add_option( "files", raw_filenames, "files to open" );

        options.parse( argc, argv );

        for ( const auto& file : raw_filenames ) {
            auto decodedName = QFile::decodeName( file.c_str() );
            if ( !decodedName.isEmpty() ) {
                const auto fileInfo = QFileInfo( decodedName );
                filenames.emplace_back( fileInfo.absoluteFilePath() );
            }
        }
    }
};

void applyStyle()
{
    const auto& config = Configuration::get();
    auto style = config.style();
    LOG_INFO << "Setting style to " << style;
    if ( style == DarkStyleKey ) {

        // based on https://gist.github.com/QuantumCD/6245215

        QColor darkGray( 53, 53, 53 );
        QColor gray( 128, 128, 128 );
        QColor black( 40, 40, 40 );
        QColor white( 240, 240, 240 );
        QColor blue( 42, 130, 218 );

        QPalette darkPalette;
        darkPalette.setColor( QPalette::Window, darkGray );
        darkPalette.setColor( QPalette::WindowText, white );
        darkPalette.setColor( QPalette::Base, black );
        darkPalette.setColor( QPalette::AlternateBase, darkGray );
        darkPalette.setColor( QPalette::ToolTipBase, blue );
        darkPalette.setColor( QPalette::ToolTipText, white );
        darkPalette.setColor( QPalette::Text, white );
        darkPalette.setColor( QPalette::Button, darkGray );
        darkPalette.setColor( QPalette::ButtonText, white );
        darkPalette.setColor( QPalette::Link, blue );
        darkPalette.setColor( QPalette::Highlight, blue );
        darkPalette.setColor( QPalette::HighlightedText, black.darker() );

        darkPalette.setColor( QPalette::Active, QPalette::Button, gray.darker() );
        darkPalette.setColor( QPalette::Disabled, QPalette::ButtonText, gray );
        darkPalette.setColor( QPalette::Disabled, QPalette::WindowText, gray );
        darkPalette.setColor( QPalette::Disabled, QPalette::Text, gray );
        darkPalette.setColor( QPalette::Disabled, QPalette::Light, darkGray );

        qApp->setStyle( QStyleFactory::create( "Fusion" ) );
        qApp->setPalette( darkPalette );
    }
    else {
        QApplication::setStyle( style );
        qApp->setStyleSheet( "" );
    }
}

int main( int argc, char* argv[] )
{
#ifdef KLOGG_USE_MIMALLOC
    mi_stats_reset();
#endif

    setApplicationAttributes();

    KloggApp app( argc, argv );

    CliParameters parameters;
    CLI::App options{ "Klogg -- fast log explorer" };
    try {
        parameters = CliParameters( options, argc, argv );
    } catch ( const CLI::ParseError& e ) {
        return options.exit( e );
    } catch ( const std::exception& e ) {
        std::cerr << "Exception of unknown type: " << e.what() << std::endl;
    }

    app.initLogger( static_cast<plog::Severity>( parameters.log_level ), parameters.log_to_file );
    app.initCrashHandler();

    LOG_INFO << "Klogg instance " << app.instanceId();

    if ( !parameters.multi_instance && app.isSecondary() ) {
        LOG_INFO << "Found another klogg, pid " << app.primaryPid();
        app.sendFilesToPrimaryInstance( parameters.filenames );
    }
    else {
        Configuration::getSynced();

        // Load the existing session if needed
        const auto& config = Configuration::get();
        plog::enableLogging( config.enableLogging(), config.loggingLevel() );

        applyStyle();

        auto startNewSession = true;
        MainWindow* mw = nullptr;
        if ( parameters.load_session
             || ( parameters.filenames.empty() && !parameters.new_session
                  && config.loadLastSession() ) ) {
            mw = app.reloadSession();
            startNewSession = false;
        }
        else {
            mw = app.newWindow();
            mw->reloadGeometry();
            LOG_DEBUG << "MainWindow created.";
            mw->show();
        }

        if ( parameters.window_width > 0 && parameters.window_height > 0 ) {
            mw->resize( parameters.window_width, parameters.window_height );
        }

        for ( const auto& filename : parameters.filenames ) {
            mw->loadInitialFile( filename, parameters.follow_file );
        }

        if ( startNewSession ) {
            app.clearInactiveSessions();
        }

        app.startBackgroundTasks();
    }

    return app.exec();
}

static void print_version()
{
    std::cout << "klogg " << kloggVersion().data() << "\n";
    std::cout << "Built " << kloggBuildDate().data() << " from " << kloggCommit().data() << "("
              << kloggGitVersion().data() << ")\n";

    std::cout << "Copyright (C) 2020 Nicolas Bonnefon, Anton Filimonov and other contributors\n";
    std::cout << "This is free software.  You may redistribute copies of it under the terms of\n";
    std::cout << "the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n";
    std::cout << "There is NO WARRANTY, to the extent permitted by law.\n";
}
