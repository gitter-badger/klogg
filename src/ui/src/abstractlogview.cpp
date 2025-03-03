/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2015 Nicolas Bonnefon
 * and other contributors
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

// This file implements the AbstractLogView base class.
// Most of the actual drawing and event management common to the two views
// is implemented in this class.  The class only calls protected virtual
// functions when view specific behaviour is desired, using the template
// pattern.

#include <cassert>
#include <iostream>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGestureEvent>
#include <QInputDialog>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QProgressDialog>
#include <QRect>
#include <QScrollBar>
#include <QtCore>
#include <utility>

#include <tbb/flow_graph.h>

#include "log.h"

#include "configuration.h"
#include "highlighterset.h"
#include "highlightersmenu.h"
#include "logmainview.h"
#include "overview.h"
#include "quickfind.h"
#include "quickfindpattern.h"

#ifdef Q_OS_WIN
#pragma warning( disable : 4244 )
#endif

namespace {
int mapPullToFollowLength( int length );
}

namespace {

int countDigits( quint64 n )
{
    if ( n == 0 )
        return 1;

    return static_cast<int>( qFloor( qLn( static_cast<qreal>( n ) ) / qLn( 10 ) + 1 ) );
}

int textWidth( const QFontMetrics& fm, const QString& text )
{
#if ( QT_VERSION >= QT_VERSION_CHECK( 5, 11, 0 ) )
    return fm.horizontalAdvance( text );
#else
    return fm.width( text );
#endif
}

} // namespace

inline void LineDrawer::addChunk( int first_col, int last_col, const QColor& fore,
                                  const QColor& back )
{
    if ( first_col < 0 ) {
        first_col = 0;
    }

    const auto length = last_col - first_col + 1;

    if ( length > 0 ) {
        chunks_.emplace_back( first_col, last_col, fore, back );
    }
}

inline void LineDrawer::addChunk( const LineChunk& chunk )
{
    addChunk( chunk.start(), chunk.end(), chunk.foreColor(), chunk.backColor() );
}

inline void LineDrawer::draw( QPainter& painter, int initialXPos, int initialYPos, int line_width,
                              const QString& line, int leftExtraBackgroundPx )
{
    QFontMetrics fm = painter.fontMetrics();
    const int fontHeight = fm.height();
    const int fontAscent = fm.ascent();

    int xPos = initialXPos;
    int yPos = initialYPos;

    for ( const auto& chunk : chunks_ ) {
        // Draw each chunk
        // LOG_DEBUG << "Chunk: " << chunk.start() << " " << chunk.length();
        const auto cutline = line.mid( chunk.start(), chunk.length() );
        const int chunk_width = textWidth( fm, cutline );
        if ( xPos == initialXPos ) {
            // First chunk, we extend the left background a bit,
            // it looks prettier.
            painter.fillRect( xPos - leftExtraBackgroundPx, yPos,
                              chunk_width + leftExtraBackgroundPx, fontHeight, chunk.backColor() );
        }
        else {
            // other chunks...
            painter.fillRect( xPos, yPos, chunk_width, fontHeight, chunk.backColor() );
        }
        painter.setPen( chunk.foreColor() );
        painter.drawText( xPos, yPos + fontAscent, cutline );
        xPos += chunk_width;
    }

    // Draw the empty block at the end of the line
    int blank_width = line_width - xPos;

    if ( blank_width > 0 )
        painter.fillRect( xPos, yPos, blank_width, fontHeight, backColor_ );
}

void DigitsBuffer::reset()
{
    LOG_DEBUG << "DigitsBuffer::reset()";

    timer_.stop();
    digits_.clear();
}

void DigitsBuffer::add( char character )
{
    LOG_DEBUG << "DigitsBuffer::add()";

    digits_.append( QChar( character ) );
    timer_.start( timeout_, this );
}

int DigitsBuffer::content()
{
    int result = digits_.toInt();
    reset();

    return result;
}

bool DigitsBuffer::isEmpty() const
{
    return digits_.isEmpty();
}

void DigitsBuffer::timerEvent( QTimerEvent* event )
{
    if ( event->timerId() == timer_.timerId() ) {
        reset();
    }
    else {
        QObject::timerEvent( event );
    }
}

AbstractLogView::AbstractLogView( const AbstractLogData* newLogData,
                                  const QuickFindPattern* const quickFindPattern, QWidget* parent )
    : QAbstractScrollArea( parent )
    , followElasticHook_( HOOK_THRESHOLD )
    , logData_( newLogData )
    , searchEnd_( newLogData->getNbLine().get() )
    , quickFindPattern_( quickFindPattern )
    , quickFind_( new QuickFind( *newLogData ) )
{
    // Create the viewport QWidget
    setViewport( nullptr );

    // Hovering
    setMouseTracking( true );

    // Init the popup menu
    createMenu();

    // Signals
    connect( quickFindPattern_, SIGNAL( patternUpdated() ), this, SLOT( handlePatternUpdated() ) );
    connect( quickFind_, SIGNAL( notify( const QFNotification& ) ), this,
             SIGNAL( notifyQuickFind( const QFNotification& ) ) );
    connect( quickFind_, SIGNAL( clearNotification() ), this,
             SIGNAL( clearQuickFindNotification() ) );

    connect( quickFind_, &QuickFind::searchDone, this, &AbstractLogView::setQuickFindResult,
             Qt::QueuedConnection );

    connect( &followElasticHook_, SIGNAL( lengthChanged() ), this, SLOT( repaint() ) );
    connect( &followElasticHook_, SIGNAL( hooked( bool ) ), this,
             SIGNAL( followModeChanged( bool ) ) );
}

AbstractLogView::~AbstractLogView()
{
    quickFind_->stopSearch();
}

//
// Received events
//

void AbstractLogView::changeEvent( QEvent* changeEvent )
{
    QAbstractScrollArea::changeEvent( changeEvent );

    // Stop the timer if the widget becomes inactive
    if ( changeEvent->type() == QEvent::ActivationChange ) {
        if ( !isActiveWindow() )
            autoScrollTimer_.stop();
    }
    viewport()->update();
}

void AbstractLogView::mousePressEvent( QMouseEvent* mouseEvent )
{
    auto line = convertCoordToLine( mouseEvent->y() );

    if ( mouseEvent->button() == Qt::LeftButton ) {
        if ( line.has_value() && mouseEvent->modifiers() & Qt::ShiftModifier ) {
            selection_.selectRangeFromPrevious( *line );
            emit updateLineNumber( *line );
            update();
        }
        else if ( line.has_value() ) {
            if ( mouseEvent->x() < bulletZoneWidthPx_ ) {
                // Mark a line if it is clicked in the left margin
                // (only if click and release in the same area)
                markingClickInitiated_ = true;
                markingClickLine_ = line;
            }
            else {
                // Select the line, and start a selection
                if ( *line < logData_->getNbLine() ) {
                    selection_.selectLine( *line );
                    emit updateLineNumber( *line );
                    emit newSelection( *line );
                }

                // Remember the click in case we're starting a selection
                selectionStarted_ = true;
                selectionStartPos_ = convertCoordToFilePos( mouseEvent->pos() );
                selectionCurrentEndPos_ = selectionStartPos_;
            }
        }

        // Invalidate our cache
        textAreaCache_.invalid_ = true;
    }
    else if ( mouseEvent->button() == Qt::RightButton ) {
        if ( line.has_value() && line >= logData_->getNbLine() ) {
            line = {};
        }

        const auto filePos = convertCoordToFilePos( mouseEvent->pos() );

        if ( line.has_value()
             && !selection_.isPortionSelected( *line, filePos.x(), filePos.x() ) ) {
            selection_.selectLine( *line );
            emit updateLineNumber( *line );
            textAreaCache_.invalid_ = true;
            emit newSelection( *line );
        }

        if ( selection_.isSingleLine() ) {
            copyAction_->setText( "&Copy this line" );

            setSearchStartAction_->setEnabled( true );
            setSearchEndAction_->setEnabled( true );

            setSelectionStartAction_->setEnabled( true );
            setSelectionEndAction_->setEnabled( !!selectionStart_ );
        }
        else {
            copyAction_->setText( "&Copy" );
            copyAction_->setStatusTip( tr( "Copy the selection" ) );

            setSearchStartAction_->setEnabled( false );
            setSearchEndAction_->setEnabled( false );

            setSelectionStartAction_->setEnabled( false );
            setSelectionEndAction_->setEnabled( false );
        }

        if ( selection_.isPortion() ) {
            findNextAction_->setEnabled( true );
            findPreviousAction_->setEnabled( true );
            addToSearchAction_->setEnabled( true );
            replaceSearchAction_->setEnabled( true );
        }
        else {
            findNextAction_->setEnabled( false );
            findPreviousAction_->setEnabled( false );
            addToSearchAction_->setEnabled( false );
            replaceSearchAction_->setEnabled( false );
        }

        auto highlightersActionGroup = new QActionGroup( this );
        connect( highlightersActionGroup, &QActionGroup::triggered, this,
                 &AbstractLogView::setHighlighterSet );

        highlightersMenu_->clear();

        populateHighlightersMenu( highlightersMenu_, highlightersActionGroup );

        // Display the popup (blocking)
        popupMenu_->exec( QCursor::pos() );

        highlightersActionGroup->deleteLater();
    }

    emit activity();
}

void AbstractLogView::mouseMoveEvent( QMouseEvent* mouseEvent )
{
    // Selection implementation
    if ( selectionStarted_ ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        QPoint thisEndPos = convertCoordToFilePos( mouseEvent->pos() );
        if ( thisEndPos != selectionCurrentEndPos_ ) {
            const auto lineNumber
                = LineNumber( static_cast<LineNumber::UnderlyingType>( thisEndPos.y() ) );
            // Are we on a different line?
            if ( selectionStartPos_.y() != thisEndPos.y() ) {
                if ( thisEndPos.y() != selectionCurrentEndPos_.y() ) {
                    // This is a 'range' selection
                    selection_.selectRange( LineNumber( static_cast<LineNumber::UnderlyingType>(
                                                selectionStartPos_.y() ) ),
                                            lineNumber );
                    emit updateLineNumber( lineNumber );
                    update();
                }
            }
            // So we are on the same line. Are we moving horizontaly?
            else if ( thisEndPos.x() != selectionCurrentEndPos_.x() ) {
                // This is a 'portion' selection
                selection_.selectPortion( lineNumber, selectionStartPos_.x(), thisEndPos.x() );
                update();
            }
            // On the same line, and moving vertically then
            else {
                // This is a 'line' selection
                selection_.selectLine( lineNumber );
                emit updateLineNumber( lineNumber );
                update();
            }
            selectionCurrentEndPos_ = thisEndPos;

            // Do we need to scroll while extending the selection?
            QRect visible = viewport()->rect();
            if ( visible.contains( mouseEvent->pos() ) )
                autoScrollTimer_.stop();
            else if ( !autoScrollTimer_.isActive() )
                autoScrollTimer_.start( 100, this );
        }
    }
    else {
        considerMouseHovering( mouseEvent->x(), mouseEvent->y() );
    }
}

void AbstractLogView::mouseReleaseEvent( QMouseEvent* mouseEvent )
{
    if ( markingClickInitiated_ ) {
        markingClickInitiated_ = false;
        const auto line = convertCoordToLine( mouseEvent->y() );
        if ( line.has_value() && line == markingClickLine_ ) {
            // Invalidate our cache
            textAreaCache_.invalid_ = true;

            emit markLines( { *line } );
        }
    }
    else {
        selectionStarted_ = false;
        if ( autoScrollTimer_.isActive() )
            autoScrollTimer_.stop();
        updateGlobalSelection();
    }
}

void AbstractLogView::mouseDoubleClickEvent( QMouseEvent* mouseEvent )
{
    if ( mouseEvent->button() == Qt::LeftButton ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        const QPoint pos = convertCoordToFilePos( mouseEvent->pos() );
        selectWordAtPosition( pos );
    }

    emit activity();
}

void AbstractLogView::timerEvent( QTimerEvent* timerEvent )
{
    if ( timerEvent->timerId() == autoScrollTimer_.timerId() ) {
        QRect visible = viewport()->rect();
        const QPoint globalPos = QCursor::pos();
        const QPoint pos = viewport()->mapFromGlobal( globalPos );
        QMouseEvent ev( QEvent::MouseMove, pos, globalPos, Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier );
        mouseMoveEvent( &ev );
        int deltaX = qMax( pos.x() - visible.left(), visible.right() - pos.x() ) - visible.width();
        int deltaY = qMax( pos.y() - visible.top(), visible.bottom() - pos.y() ) - visible.height();
        int delta = qMax( deltaX, deltaY );

        if ( delta >= 0 ) {
            if ( delta < 7 )
                delta = 7;
            int timeout = 4900 / ( delta * delta );
            autoScrollTimer_.start( timeout, this );

            if ( deltaX > 0 )
                horizontalScrollBar()->triggerAction( pos.x() < visible.center().x()
                                                          ? QAbstractSlider::SliderSingleStepSub
                                                          : QAbstractSlider::SliderSingleStepAdd );

            if ( deltaY > 0 )
                verticalScrollBar()->triggerAction( pos.y() < visible.center().y()
                                                        ? QAbstractSlider::SliderSingleStepSub
                                                        : QAbstractSlider::SliderSingleStepAdd );
        }
    }
    QAbstractScrollArea::timerEvent( timerEvent );
}

void AbstractLogView::moveSelectionUp()
{
    int delta = qMin( -1, -digitsBuffer_.content() );
    disableFollow();
    moveSelection( delta );
}

void AbstractLogView::moveSelectionDown()
{
    int delta = qMax( 1, digitsBuffer_.content() );
    disableFollow();
    moveSelection( delta );
}

void AbstractLogView::keyPressEvent( QKeyEvent* keyEvent )
{
    LOG_DEBUG << "keyPressEvent received";

    const auto controlModifier = keyEvent->modifiers().testFlag( Qt::ControlModifier );
    const auto shiftModifier = keyEvent->modifiers().testFlag( Qt::ShiftModifier );
    const auto altModifier = keyEvent->modifiers().testFlag( Qt::AltModifier );
    const auto noModifier = keyEvent->modifiers() == Qt::NoModifier;

    const auto jumpToBottomLine = [ this ]() {
        disableFollow();
        const auto line = LineNumber( logData_->getNbLine().get() ) - 1_lcount;
        selection_.selectLine( line );
        emit updateLineNumber( line );
        emit newSelection( line );
        jumpToBottom();
    };

    if ( keyEvent->key() == Qt::Key_Up && noModifier )
        moveSelectionUp();
    else if ( keyEvent->key() == Qt::Key_Down && noModifier )
        moveSelectionDown();
    else if ( keyEvent->key() == Qt::Key_Up && controlModifier )
        verticalScrollBar()->triggerAction( QScrollBar::SliderSingleStepSub );
    else if ( keyEvent->key() == Qt::Key_Down && controlModifier )
        verticalScrollBar()->triggerAction( QScrollBar::SliderSingleStepAdd );
    else if ( keyEvent->key() == Qt::Key_Left && noModifier )
        horizontalScrollBar()->triggerAction( QScrollBar::SliderPageStepSub );
    else if ( keyEvent->key() == Qt::Key_Right && noModifier )
        horizontalScrollBar()->triggerAction( QScrollBar::SliderPageStepAdd );
    else if ( keyEvent->key() == Qt::Key_Home && !controlModifier )
        jumpToStartOfLine();
    else if ( keyEvent->key() == Qt::Key_End && !controlModifier )
        jumpToRightOfScreen();
    else if ( keyEvent->key() == Qt::Key_End && controlModifier ) {
        jumpToBottomLine(); // Same as G
    }
    else if ( keyEvent->key() == Qt::Key_Home && controlModifier )
        selectAndDisplayLine( 0_lnum );
    else if ( keyEvent->key() == Qt::Key_F3 && !shiftModifier )
        emit searchNext(); // duplicate of 'n' action.
    else if ( keyEvent->key() == Qt::Key_F3 && shiftModifier )
        emit searchPrevious(); // duplicate of 'N' action.
    else if ( keyEvent->key() == Qt::Key_Space && noModifier )
        emit exitView();
    else {
        switch ( keyEvent->key() ) {
        case Qt::Key_J:
            moveSelectionDown();
            break;
        case Qt::Key_K:
            moveSelectionUp();
            break;
        case Qt::Key_H:
            horizontalScrollBar()->triggerAction( QScrollBar::SliderSingleStepSub );
            break;
        case Qt::Key_L:
            horizontalScrollBar()->triggerAction( QScrollBar::SliderSingleStepAdd );
            break;
        case Qt::Key_AsciiCircum:
            jumpToStartOfLine();
            break;
        case Qt::Key_Dollar:
            jumpToEndOfLine();
            break;
        case Qt::Key_Asterisk:
        case Qt::Key_Period:
            // Use the selected 'word' and search forward
            findNextSelected();
            break;
        case Qt::Key_Slash:
        case Qt::Key_Comma:
            // Use the selected 'word' and search backward
            findPreviousSelected();
            break;
        case Qt::Key_M: {
            markSelected();
            break;
        }
        case Qt::Key_G:
            if ( controlModifier ) {
                if ( shiftModifier ) {
                    emit searchPrevious();
                }
                else {
                    emit searchNext();
                }
                break;
            }
            if ( shiftModifier ) {
                jumpToBottomLine();
            }
            else {
                bool isLineSelected = true;
                int newLine = 0;
                if ( altModifier ) {
                    newLine = QInputDialog::getInt( this, "Jump to line", "Line", 1, 1,
                                                    static_cast<int>( logData_->getNbLine().get() ),
                                                    1, &isLineSelected );
                    newLine -= 1;
                }
                else {
                    newLine = qMax( 0, digitsBuffer_.content() - 1 );
                }

                auto lineToSelect
                    = LineNumber( static_cast<LineNumber::UnderlyingType>( newLine ) );
                if ( lineToSelect >= logData_->getNbLine() ) {
                    lineToSelect = lineToSelect - 1_lcount;
                }

                if ( isLineSelected ) {
                    selectAndDisplayLine( lineToSelect );
                }
            }
            break;
        case Qt::Key_N:
            if ( shiftModifier ) {
                emit searchPrevious();
            }
            else {
                emit searchNext();
            }
            break;
        default:
            const auto text = keyEvent->text();

            if ( keyEvent->modifiers() == Qt::NoModifier && text.count() == 1 ) {
                const auto character = text.at( 0 ).toLatin1();
                if ( ( ( character > '0' ) && ( character <= '9' ) )
                     || ( !digitsBuffer_.isEmpty() && character == '0' ) ) {
                    // Adds the digit to the timed buffer
                    digitsBuffer_.add( character );
                }
                else if ( digitsBuffer_.isEmpty() && character == '0' ) {
                    jumpToStartOfLine();
                }
            }
            else {
                keyEvent->ignore();
            }
        }
    }

    if ( keyEvent->isAccepted() ) {
        emit activity();
    }
    else {
        // Only pass bare keys to the superclass this is so that
        // shortcuts such as Ctrl+Alt+Arrow are handled by the parent.
        LOG_DEBUG << std::hex << keyEvent->modifiers();
        if ( keyEvent->modifiers() == Qt::NoModifier
             || keyEvent->modifiers() == Qt::KeypadModifier ) {
            QAbstractScrollArea::keyPressEvent( keyEvent );
        }
    }
}

void AbstractLogView::wheelEvent( QWheelEvent* wheelEvent )
{
    emit activity();

    int y_delta = 0;
    auto pixel_delta = wheelEvent->pixelDelta();

    if ( pixel_delta.isNull() ) {
        y_delta = static_cast<int>(
            std::floor( static_cast<float>( wheelEvent->angleDelta().y() ) / 0.7f ) );
    }
    else {
        y_delta = pixel_delta.y();
    }

    if ( y_delta == 0 ) {
        QAbstractScrollArea::wheelEvent( wheelEvent );
        return;
    }

    // LOG_DEBUG << "wheelEvent";

    // This is to handle the case where follow mode is on, but the user
    // has moved using the scroll bar. We take them back to the bottom.
    if ( followMode_ )
        jumpToBottom();

    if ( verticalScrollBar()->value() == verticalScrollBar()->maximum() ) {
        // First see if we need to block the elastic (on Mac)
        if ( wheelEvent->phase() == Qt::ScrollBegin )
            followElasticHook_.hold();
        else if ( wheelEvent->phase() == Qt::ScrollEnd )
            followElasticHook_.release();

        // LOG_DEBUG << "Elastic " << y_delta;
        followElasticHook_.move( -y_delta );
    }

    // LOG_DEBUG << "Length = " << followElasticHook_.length();
    if ( followElasticHook_.length() == 0 && !followElasticHook_.isHooked() ) {
        QAbstractScrollArea::wheelEvent( wheelEvent );
    }
}

void AbstractLogView::resizeEvent( QResizeEvent* )
{
    if ( logData_ == nullptr )
        return;

    LOG_DEBUG << "resizeEvent received";

    updateDisplaySize();
}

bool AbstractLogView::event( QEvent* e )
{
    LOG_DEBUG << "Event! Type: " << e->type();

    // Make sure we ignore the gesture events as
    // they seem to be accepted by default.
    if ( e->type() == QEvent::Gesture ) {
        auto gesture_event = dynamic_cast<QGestureEvent*>( e );
        if ( gesture_event ) {
            const auto gestures = gesture_event->gestures();
            for ( QGesture* gesture : gestures ) {
                LOG_DEBUG << "Gesture: " << gesture->gestureType();
                gesture_event->ignore( gesture );
            }

            // Ensure the event is sent up to parents who might care
            return false;
        }
    }

    return QAbstractScrollArea::event( e );
}

void AbstractLogView::scrollContentsBy( int dx, int dy )
{
    LOG_DEBUG << "scrollContentsBy received " << dy << "position " << verticalScrollBar()->value();

    const auto last_top_line = ( logData_->getNbLine() - getNbVisibleLines() );
    const auto scrollPosition
        = static_cast<LinesCount::UnderlyingType>( verticalScrollBar()->value() );
    if ( ( last_top_line.get() > 0 ) && scrollPosition > last_top_line.get() ) {
        // The user is going further than the last line, we need to lock the last line at the bottom
        LOG_DEBUG << "scrollContentsBy beyond!";
        firstLine_ = LineNumber( scrollPosition );
        lastLineAligned_ = true;
    }
    else {
        firstLine_ = LineNumber( scrollPosition );
        lastLineAligned_ = false;
    }

    firstCol_ = ( firstCol_ - dx ) > 0 ? firstCol_ - dx : 0;
    const auto last_line = firstLine_ + getNbVisibleLines();

    // Update the overview if we have one
    if ( overview_ != nullptr )
        overview_->updateCurrentPosition( firstLine_, last_line );

    // Are we hovering over a new line?
    const QPoint mouse_pos = mapFromGlobal( QCursor::pos() );
    considerMouseHovering( mouse_pos.x(), mouse_pos.y() );

    // Redraw
    update();
}

void AbstractLogView::paintEvent( QPaintEvent* paintEvent )
{
    const QRect invalidRect = paintEvent->rect();
    if ( ( invalidRect.isEmpty() ) || ( logData_ == nullptr ) )
        return;

    LOG_DEBUG << "paintEvent received, firstLine_=" << firstLine_
              << " lastLineAligned_=" << lastLineAligned_ << " rect: " << invalidRect.topLeft().x()
              << ", " << invalidRect.topLeft().y() << ", " << invalidRect.bottomRight().x() << ", "
              << invalidRect.bottomRight().y();

#ifdef GLOGG_PERF_MEASURE_FPS
    static uint32_t maxline = logData_->getNbLine();
    if ( !perfCounter_.addEvent() && logData_->getNbLine() > maxline ) {
        LOG_WARNING << "Redraw per second: " << perfCounter_.readAndReset()
                    << " lines: " << logData_->getNbLine();
        perfCounter_.addEvent();
        maxline = logData_->getNbLine();
    }
#endif

    auto start = std::chrono::system_clock::now();

    // Can we use our cache?
    auto delta_y = textAreaCache_.first_line_.get() - firstLine_.get();

    if ( textAreaCache_.invalid_ || ( textAreaCache_.first_column_ != firstCol_ ) ) {
        // Force a full redraw
        delta_y = std::numeric_limits<decltype( delta_y )>::max();
    }

    if ( delta_y != 0 ) {
        // Full or partial redraw
        drawTextArea( &textAreaCache_.pixmap_ );

        textAreaCache_.invalid_ = false;
        textAreaCache_.first_line_ = firstLine_;
        textAreaCache_.first_column_ = firstCol_;

        LOG_DEBUG << "End of writing "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now() - start )
                         .count();
    }
    else {
        // Use the cache as is: nothing to do!
    }

    // Height including the potentially invisible last line
    const auto whole_height = static_cast<int>( getNbVisibleLines().get() ) * charHeight_;
    // Height in pixels of the "pull to follow" bottom bar.
    const auto pullToFollowHeight
        = mapPullToFollowLength( followElasticHook_.length() )
          + ( followElasticHook_.isHooked()
                  ? ( whole_height - viewport()->height() ) + PULL_TO_FOLLOW_HOOKED_HEIGHT
                  : 0 );

    if ( pullToFollowHeight && ( pullToFollowCache_.nb_columns_ != getNbVisibleCols() ) ) {
        LOG_DEBUG << "Drawing pull to follow bar";
        pullToFollowCache_.pixmap_
            = drawPullToFollowBar( viewport()->width(), viewport()->devicePixelRatio() );
        pullToFollowCache_.nb_columns_ = getNbVisibleCols();
    }

    QPainter devicePainter( viewport() );
    int drawingTopPosition = -pullToFollowHeight;
    int drawingPullToFollowTopPosition = drawingTopPosition + whole_height;

    // This is to cover the special case where there is less than a screenful
    // worth of data, we want to see the document from the top, rather than
    // pushing the first couple of lines above the viewport.
    if ( followElasticHook_.isHooked() && ( logData_->getNbLine() < getNbVisibleLines() ) ) {
        drawingTopOffset_ = 0;
        drawingTopPosition
            += ( whole_height - viewport()->height() ) + PULL_TO_FOLLOW_HOOKED_HEIGHT;
        drawingPullToFollowTopPosition
            = drawingTopPosition + viewport()->height() - PULL_TO_FOLLOW_HOOKED_HEIGHT;
    }
    // This is the case where the user is on the 'extra' slot at the end
    // and is aligned on the last line (but no elastic shown)
    else if ( lastLineAligned_ && !followElasticHook_.isHooked() ) {
        drawingTopOffset_ = -( whole_height - viewport()->height() );
        drawingTopPosition += drawingTopOffset_;
        drawingPullToFollowTopPosition = drawingTopPosition + whole_height;
    }
    else {
        drawingTopOffset_ = -pullToFollowHeight;
    }

    devicePainter.drawPixmap( 0, drawingTopPosition, textAreaCache_.pixmap_ );

    // Draw the "pull to follow" zone if needed
    if ( pullToFollowHeight ) {
        devicePainter.drawPixmap( 0, drawingPullToFollowTopPosition, pullToFollowCache_.pixmap_ );
    }

    LOG_DEBUG << "End of repaint "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now() - start )
                     .count();
}

// These two functions are virtual and this implementation is clearly
// only valid for a non-filtered display.
// We count on the 'filtered' derived classes to override them.
LineNumber AbstractLogView::displayLineNumber( LineNumber lineNumber ) const
{
    return lineNumber + 1_lcount; // show a 1-based index
}

LineNumber AbstractLogView::lineIndex( LineNumber lineNumber ) const
{
    return lineNumber;
}

LineNumber AbstractLogView::maxDisplayLineNumber() const
{
    return LineNumber( logData_->getNbLine().get() );
}

void AbstractLogView::setOverview( Overview* overview, OverviewWidget* overview_widget )
{
    overview_ = overview;
    overviewWidget_ = overview_widget;

    if ( overviewWidget_ ) {
        connect( overviewWidget_, &OverviewWidget::lineClicked, this,
                 &AbstractLogView::jumpToLine );
    }
    refreshOverview();
}

LineNumber AbstractLogView::getViewPosition() const
{
    LineNumber line;

    const auto selectedLine = selection_.selectedLine();
    if ( selectedLine.has_value() ) {
        line = *selectedLine;
    }
    else {
        // Middle of the view
        line = firstLine_ + LinesCount( getNbVisibleLines().get() / 2 );
    }

    return line;
}

void AbstractLogView::searchUsingFunction(
    void ( QuickFind::*search_function )( Selection, QuickFindMatcher ) )
{
    disableFollow();
    ( quickFind_->*search_function )( selection_, quickFindPattern_->getMatcher() );
}

void AbstractLogView::setQuickFindResult( bool hasMatch, Portion portion )
{
    if ( portion.isValid() ) {
        LOG_DEBUG << "search " << portion.line();
        displayLine( portion.line() );
        selection_.selectPortion( portion );
        emit updateLineNumber( portion.line() );
    }
    else if ( !hasMatch ) {
        selection_.clear();
    }
}

void AbstractLogView::searchForward()
{
    searchUsingFunction( &QuickFind::searchForward );
}

void AbstractLogView::searchBackward()
{
    searchUsingFunction( &QuickFind::searchBackward );
}

void AbstractLogView::incrementallySearchForward()
{
    searchUsingFunction( &QuickFind::incrementallySearchForward );
}

void AbstractLogView::incrementallySearchBackward()
{
    searchUsingFunction( &QuickFind::incrementallySearchBackward );
}

void AbstractLogView::incrementalSearchAbort()
{
    selection_ = quickFind_->incrementalSearchAbort();
    emit changeQuickFind( "", QuickFindMux::Forward );
}

void AbstractLogView::incrementalSearchStop()
{
    auto oldSelection = quickFind_->incrementalSearchStop();
    if ( selection_.isEmpty() ) {
        selection_ = oldSelection;
    }
}

void AbstractLogView::allowFollowMode( bool allow )
{
    followElasticHook_.allowHook( allow );
}

void AbstractLogView::followSet( bool checked )
{
    followMode_ = checked;
    followElasticHook_.hook( checked );
    update();
    if ( checked )
        jumpToBottom();
}

void AbstractLogView::refreshOverview()
{
    assert( overviewWidget_ );

    // Create space for the Overview if needed
    if ( ( getOverview() != nullptr ) && getOverview()->isVisible() ) {
        setViewportMargins( 0, 0, OVERVIEW_WIDTH, 0 );
        overviewWidget_->show();
    }
    else {
        setViewportMargins( 0, 0, 0, 0 );
        overviewWidget_->hide();
    }
}

// Reset the QuickFind when the pattern is changed.
void AbstractLogView::handlePatternUpdated()
{
    LOG_DEBUG << "AbstractLogView::handlePatternUpdated()";

    quickFind_->resetLimits();
    update();
}

// OR the current selection with the current search expression
void AbstractLogView::addToSearch()
{
    if ( selection_.isPortion() ) {
        LOG_DEBUG << "AbstractLogView::addToSearch()";
        emit addToSearch( selection_.getSelectedText( logData_ ) );
    }
    else {
        LOG_ERROR << "AbstractLogView::addToSearch called for a wrong type of selection";
    }
}

// Replace the current search expression with the current selection
void AbstractLogView::replaceSearch()
{
    if ( selection_.isPortion() ) {
        LOG_DEBUG << "AbstractLogView::replaceSearch()";
        emit replaceSearch( selection_.getSelectedText( logData_ ) );
    }
    else {
        LOG_ERROR << "AbstractLogView::replaceSearch called for a wrong type of selection";
    }
}

// Find next occurrence of the selected text (*)
void AbstractLogView::findNextSelected()
{
    // Use the selected 'word' and search forward
    if ( selection_.isPortion() ) {
        emit changeQuickFind( selection_.getSelectedText( logData_ ), QuickFindMux::Forward );
        emit searchNext();
    }
}

// Find next previous of the selected text (#)
void AbstractLogView::findPreviousSelected()
{
    if ( selection_.isPortion() ) {
        emit changeQuickFind( selection_.getSelectedText( logData_ ), QuickFindMux::Backward );
        emit searchNext();
    }
}

// Copy the selection to the clipboard
void AbstractLogView::copy()
{
    try {
        auto clipboard = QApplication::clipboard();
        auto text = selection_.getSelectedText( logData_ );
        text.replace( QChar::Null, QChar::Space );
        clipboard->setText( text );
    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
    }
}

void AbstractLogView::markSelected()
{
    auto lines = selection_.getLines();
    if ( !lines.empty() ) {
        emit markLines( lines );
    }
}

void AbstractLogView::saveToFile()
{
    auto filename = QFileDialog::getSaveFileName( this, "Save content" );
    if ( filename.isEmpty() ) {
        return;
    }

    const auto totalLines = logData_->getNbLine();
    QSaveFile saveFile{ filename };
    saveFile.open( QIODevice::WriteOnly | QIODevice::Truncate );
    if ( !saveFile.isOpen() ) {
        LOG_ERROR << "Failed to open file to save";
        return;
    }

    QProgressDialog progressDialog( this );
    progressDialog.setLabelText( QString( "Saving content to %1" ).arg( filename ) );

    std::vector<std::pair<LineNumber, LinesCount>> offsets;
    auto lineOffset = 0_lnum;
    const auto chunkSize = 5000_lcount;

    for ( ; lineOffset + chunkSize < LineNumber( totalLines.get() );
          lineOffset += LineNumber( chunkSize.get() ) ) {
        offsets.emplace_back( lineOffset, chunkSize );
    }
    offsets.emplace_back( lineOffset, LinesCount( totalLines.get() % chunkSize.get() ) );

    QTextCodec* codec = logData_->getDisplayEncoding();
    if ( !codec ) {
        codec = QTextCodec::codecForName( "utf-8" );
    }

    AtomicFlag interruptRequest;

    progressDialog.setRange( 0, static_cast<int>( offsets.size() + 1 ) );
    connect( &progressDialog, &QProgressDialog::canceled,
             [ &interruptRequest ]() { interruptRequest.set(); } );

    tbb::flow::graph saveFileGraph;
    using LinesData = std::pair<std::vector<QString>, bool>;
    auto lineReader = tbb::flow::input_node<LinesData>(
        saveFileGraph,
        [ this, &offsets, &interruptRequest, &progressDialog, offsetIndex = 0u,
          finalLine = false ]( tbb::flow_control& fc ) mutable -> LinesData {
            if ( !interruptRequest && offsetIndex < offsets.size() ) {
                const auto& offset = offsets.at( offsetIndex );
                LinesData lines{ logData_->getLines( offset.first, offset.second ), true };
                for ( auto& l : lines.first ) {
#if !defined( Q_OS_WIN )
                    l.append( QChar::CarriageReturn );
#endif
                    l.append( QChar::LineFeed );
                }

                offsetIndex++;
                progressDialog.setValue( static_cast<int>( offsetIndex ) );
                return lines;
            }
            else if ( !finalLine ) {
                finalLine = true;
            }
            else {
                fc.stop();
            }

            return {};
        } );

    auto lineWriter = tbb::flow::function_node<LinesData, tbb::flow::continue_msg>(
        saveFileGraph, 1,
        [ &interruptRequest, &codec, &saveFile, &progressDialog,
          linesCount = 0u ]( const LinesData& lines ) mutable {
            if ( !lines.second ) {
                if ( !interruptRequest ) {
                    saveFile.commit();
                    linesCount++;
                }

                progressDialog.finished( static_cast<int>( linesCount ) );
                return tbb::flow::continue_msg{};
            }

            for ( const auto& l : lines.first ) {

                const auto encodedLine = codec->fromUnicode( l );
                const auto written = saveFile.write( encodedLine );

                if ( written != encodedLine.size() ) {
                    LOG_ERROR << "Saving file write failed";
                    interruptRequest.set();
                    return tbb::flow::continue_msg{};
                }

                linesCount++;
            }
            return tbb::flow::continue_msg{};
        } );

    tbb::flow::make_edge( lineReader, lineWriter );

    progressDialog.setWindowModality( Qt::ApplicationModal );
    progressDialog.open();

    lineReader.activate();
    saveFileGraph.wait_for_all();
}

void AbstractLogView::updateSearchLimits()
{
    textAreaCache_.invalid_ = true;
    update();

    emit changeSearchLimits( searchStart_, searchEnd_ );
}

void AbstractLogView::setSearchStart()
{
    const auto selectedLine = selection_.selectedLine();
    searchStart_
        = selectedLine.has_value() ? displayLineNumber( *selectedLine ) - 1_lcount : 0_lnum;
    updateSearchLimits();
}

void AbstractLogView::setSearchEnd()
{
    const auto selectedLine = selection_.selectedLine();
    searchEnd_ = selectedLine.has_value() ? displayLineNumber( *selectedLine )
                                          : LineNumber( logData_->getNbLine().get() );
    updateSearchLimits();
}

void AbstractLogView::setSelectionStart()
{
    selectionStart_ = selection_.selectedLine();
}

void AbstractLogView::setSelectionEnd()
{
    const auto selectionEnd = selection_.selectedLine();

    if ( selectionStart_ && selectionEnd ) {
        selection_.selectRange( *selectionStart_, *selectionEnd );
        selectionStart_ = {};

        textAreaCache_.invalid_ = true;
        update();
    }
}

//
// Public functions
//

void AbstractLogView::updateData()
{
    LOG_DEBUG << "AbstractLogView::updateData";

    const auto lastLineNumber = LineNumber( logData_->getNbLine().get() );

    // Check the top Line is within range
    if ( firstLine_ >= lastLineNumber ) {
        firstLine_ = 0_lnum;
        firstCol_ = 0;
        verticalScrollBar()->setValue( 0 );
        horizontalScrollBar()->setValue( 0 );
    }

    // Crop selection if it become out of range
    selection_.crop( lastLineNumber - 1_lcount );

    // Adapt the scroll bars to the new content
    updateScrollBars();

    // Calculate the index of the last line shown
    LineNumber last_line = qMin( lastLineNumber, firstLine_ + getNbVisibleLines() );

    // Reset the QuickFind in case we have new stuff to search into
    quickFind_->resetLimits();

    if ( followMode_ )
        jumpToBottom();

    // Update the overview if we have one
    if ( overview_ != nullptr )
        overview_->updateCurrentPosition( firstLine_, last_line );

    textAreaCache_.invalid_ = true;
    update();
}

void AbstractLogView::updateDisplaySize()
{
    // Font is assumed to be mono-space (is restricted by options dialog)
    const auto fm = fontMetrics();
    charHeight_ = std::max( fm.height(), 1 );
    charWidth_ = textWidth( fm, QString( "a" ) );

    // Update the scroll bars
    updateScrollBars();
    verticalScrollBar()->setPageStep( static_cast<int>( getNbVisibleLines().get() ) );

    if ( followMode_ )
        jumpToBottom();

    LOG_DEBUG << "viewport.width()=" << viewport()->width();
    LOG_DEBUG << "viewport.height()=" << viewport()->height();
    LOG_DEBUG << "width()=" << width();
    LOG_DEBUG << "height()=" << height();

    if ( overviewWidget_ )
        overviewWidget_->setGeometry( viewport()->width() + 2, 1, OVERVIEW_WIDTH - 1,
                                      viewport()->height() );

    // Our text area cache is now invalid
    textAreaCache_.invalid_ = true;
    textAreaCache_.pixmap_ = QPixmap{ viewport()->width() * viewport()->devicePixelRatio(),
                                      static_cast<int32_t>( getNbVisibleLines().get() )
                                          * charHeight_ * viewport()->devicePixelRatio() };
    textAreaCache_.pixmap_.setDevicePixelRatio( viewport()->devicePixelRatio() );
}

LineNumber AbstractLogView::getTopLine() const
{
    return firstLine_;
}

QString AbstractLogView::getSelection() const
{
    return selection_.getSelectedText( logData_ );
}

bool AbstractLogView::isPartialSelection() const
{
    return selection_.isPortion();
}

void AbstractLogView::selectAll()
{
    selection_.selectRange( 0_lnum, LineNumber( logData_->getNbLine().get() ) - 1_lcount );
    textAreaCache_.invalid_ = true;
    update();
}

void AbstractLogView::selectAndDisplayLine( LineNumber line )
{
    disableFollow();
    selection_.selectLine( line );
    displayLine( line );
    emit updateLineNumber( line );
    emit newSelection( line );
}

// The difference between this function and displayLine() is quite
// subtle: this one always jump, even if the line passed is visible.
void AbstractLogView::jumpToLine( LineNumber line )
{
    // Put the selected line in the middle if possible
    const auto newTopLine = line - LinesCount( getNbVisibleLines().get() / 2 );
    // This will also trigger a scrollContents event
    verticalScrollBar()->setValue( static_cast<int>( newTopLine.get() ) );
}

void AbstractLogView::setLineNumbersVisible( bool lineNumbersVisible )
{
    lineNumbersVisible_ = lineNumbersVisible;
}

void AbstractLogView::forceRefresh()
{
    // Invalidate our cache
    textAreaCache_.invalid_ = true;
}

void AbstractLogView::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchStart_ = startLine;
    searchEnd_ = endLine;

    textAreaCache_.invalid_ = true;
    update();
}

//
// Private functions
//

// Returns the number of lines visible in the viewport
LinesCount AbstractLogView::getNbVisibleLines() const
{
    return LinesCount(
        static_cast<LinesCount::UnderlyingType>( viewport()->height() / charHeight_ + 1 ) );
}

// Returns the number of columns visible in the viewport
int AbstractLogView::getNbVisibleCols() const
{
    return ( viewport()->width() - leftMarginPx_ ) / charWidth_ + 1;
}

// Converts the mouse x, y coordinates to the line number in the file
OptionalLineNumber AbstractLogView::convertCoordToLine( int yPos ) const
{
    const int line
        = static_cast<int>( firstLine_.get() ) + ( yPos - drawingTopOffset_ ) / charHeight_;
    return line >= 0
               ? OptionalLineNumber( LineNumber{ static_cast<LineNumber::UnderlyingType>( line ) } )
               : OptionalLineNumber{};
}

// Converts the mouse x, y coordinates to the char coordinates (in the file)
// This function ensure the pos exists in the file.
QPoint AbstractLogView::convertCoordToFilePos( const QPoint& pos ) const
{
    auto line = convertCoordToLine( pos.y() ).value_or( LineNumber{} );
    if ( line >= logData_->getNbLine() )
        line = LineNumber( logData_->getNbLine().get() ) - 1_lcount;

    QFontMetrics fm = fontMetrics();
    const auto lineText = logData_->getExpandedLineString( line );

    auto column = 0;
    for ( ; column < lineText.length(); ++column ) {
        if ( textWidth( fm, lineText.mid( firstCol_, column ) ) + leftMarginPx_ >= pos.x() ) {
            break;
        }
    }
    column += ( firstCol_ - 1 );

    const auto length = lineText.length();

    if ( column >= length )
        column = length - 1;
    if ( column < 0 )
        column = 0;

    LOG_DEBUG << "AbstractLogView::convertCoordToFilePos col=" << column << " line=" << line;
    QPoint point( column, static_cast<int>( line.get() ) );

    return point;
}

// Makes the widget adjust itself to display the passed line.
// Doing so, it will throw itself a scrollContents event.
void AbstractLogView::displayLine( LineNumber line )
{
    // If the line is already the screen
    if ( ( line >= firstLine_ ) && ( line < ( firstLine_ + getNbVisibleLines() ) ) ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        // ... don't scroll and just repaint
        update();
    }
    else {
        jumpToLine( line );
    }

    const auto portion = selection_.getPortionForLine( line );
    if ( portion.isValid() ) {
        horizontalScrollBar()->setValue( portion.endColumn() - getNbVisibleCols() + 1 );
    }
}

// Move the selection up and down by the passed number of lines
void AbstractLogView::moveSelection( int delta )
{
    LOG_DEBUG << "AbstractLogView::moveSelection delta=" << delta;

    auto selection = selection_.getLines();
    LineNumber new_line;

    if ( !selection.empty() ) {
        if ( delta < 0 )
            new_line = selection.front()
                       - LinesCount( static_cast<LinesCount::UnderlyingType>( qAbs( delta ) ) );
        else
            new_line
                = selection.back() + LinesCount( static_cast<LinesCount::UnderlyingType>( delta ) );
    }

    if ( new_line >= logData_->getNbLine() ) {
        new_line = LineNumber( logData_->getNbLine().get() ) - 1_lcount;
    }

    // Select and display the new line
    selection_.selectLine( new_line );
    displayLine( new_line );
    emit updateLineNumber( new_line );
    emit newSelection( new_line );
}

// Make the start of the lines visible
void AbstractLogView::jumpToStartOfLine()
{
    horizontalScrollBar()->setValue( 0 );
}

// Make the end of the lines in the selection visible
void AbstractLogView::jumpToEndOfLine()
{
    const auto selection = selection_.getLines();

    // Search the longest line in the selection
    const auto max_length = std::accumulate(
        selection.cbegin(), selection.cend(), 0_length, [ this ]( auto currentMax, auto line ) {
            return qMax( currentMax, logData_->getLineLength( LineNumber( line ) ) );
        } );
    horizontalScrollBar()->setValue( max_length.get() - getNbVisibleCols() );
}

// Make the end of the lines on the screen visible
void AbstractLogView::jumpToRightOfScreen()
{
    const auto nbVisibleLines = getNbVisibleLines();

    std::vector<LineNumber::UnderlyingType> visibleLinesNumbers;
    visibleLinesNumbers.resize( nbVisibleLines.get() );
    std::iota( visibleLinesNumbers.begin(), visibleLinesNumbers.end(), firstLine_.get() );

    std::vector<LineLength> lineLengths;
    lineLengths.reserve( visibleLinesNumbers.size() );

    std::transform(
        visibleLinesNumbers.begin(), visibleLinesNumbers.end(), std::back_inserter( lineLengths ),
        [ this ]( const auto& line ) { return logData_->getLineLength( LineNumber( line ) ); } );

    const auto max_length = *std::max_element( lineLengths.begin(), lineLengths.end() );
    horizontalScrollBar()->setValue( max_length.get() - getNbVisibleCols() );
}

// Jump to the first line
void AbstractLogView::jumpToTop()
{
    // This will also trigger a scrollContents event
    verticalScrollBar()->setValue( 0 );
    update(); // in case the screen hasn't moved
}

// Jump to the last line
void AbstractLogView::jumpToBottom()
{
    const auto new_top_line
        = qMax( logData_->getNbLine().get() - getNbVisibleLines().get() + 1, 0U );

    // This will also trigger a scrollContents event
    verticalScrollBar()->setValue( static_cast<int>( new_top_line ) );

    textAreaCache_.invalid_ = true;
    update();
}

// Select the word under the given position
void AbstractLogView::selectWordAtPosition( const QPoint& pos )
{
    const auto lineNumber = LineNumber( static_cast<LineNumber::UnderlyingType>( pos.y() ) );
    const QString line = logData_->getExpandedLineString( lineNumber );

    const int clickPos = pos.x();

    const auto isWordSeparator = []( QChar c ) {
        return !c.isLetterOrNumber() && c.category() != QChar::Punctuation_Connector;
    };

    if ( line.isEmpty() || isWordSeparator( line[ clickPos ] ) ) {
        return;
    }

    const auto wordStart
        = std::find_if( line.rbegin() + line.size() - clickPos, line.rend(), isWordSeparator );
    const auto selectionStart = static_cast<int>( std::distance( line.begin(), wordStart.base() ) );

    const auto wordEnd = std::find_if( line.begin() + clickPos, line.end(), isWordSeparator );
    const auto selectionEnd = static_cast<int>( std::distance( line.begin(), wordEnd ) - 1 );

    selection_.selectPortion( lineNumber, selectionStart, selectionEnd );
    updateGlobalSelection();
    update();
}

// Update the system global (middle click) selection (X11 only)
void AbstractLogView::updateGlobalSelection()
{
    try {
        auto clipboard = QApplication::clipboard();

        // Updating it only for "non-trivial" (range or portion) selections
        if ( !selection_.isSingleLine() )
            clipboard->setText( selection_.getSelectedText( logData_ ), QClipboard::Selection );
    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
    }
}

// Create the pop-up menu
void AbstractLogView::createMenu()
{
    copyAction_ = new QAction( tr( "&Copy" ), this );
    // No text as this action title depends on the type of selection
    connect( copyAction_, &QAction::triggered, [ this ]( auto ) { this->copy(); } );

    markAction_ = new QAction( tr( "&Mark" ), this );
    connect( markAction_, &QAction::triggered, [ this ]( auto ) { this->markSelected(); } );

    saveToFileAction_ = new QAction( tr( "Save to file" ), this );
    connect( saveToFileAction_, &QAction::triggered, [ this ]( auto ) { this->saveToFile(); } );

    // For '#' and '*', shortcuts doesn't seem to work but
    // at least it displays them in the menu, we manually handle those keys
    // as keys event anyway (in keyPressEvent).
    findNextAction_ = new QAction( tr( "Find &next" ), this );
    findNextAction_->setShortcut( Qt::Key_Asterisk );
    findNextAction_->setStatusTip( tr( "Find the next occurrence" ) );
    connect( findNextAction_, &QAction::triggered, [ this ]( auto ) { this->findNextSelected(); } );

    findPreviousAction_ = new QAction( tr( "Find &previous" ), this );
    findPreviousAction_->setShortcut( tr( "/" ) );
    findPreviousAction_->setStatusTip( tr( "Find the previous occurrence" ) );
    connect( findPreviousAction_, &QAction::triggered,
             [ this ]( auto ) { this->findPreviousSelected(); } );

    addToSearchAction_ = new QAction( tr( "&Add to search" ), this );
    addToSearchAction_->setStatusTip( tr( "Add the selection to the current search" ) );
    connect( addToSearchAction_, &QAction::triggered, [ this ]( auto ) { this->addToSearch(); } );

    replaceSearchAction_ = new QAction( tr( "&Replace search" ), this );
    replaceSearchAction_->setStatusTip( tr( "Replace the search expression with the selection" ) );
    connect( replaceSearchAction_, &QAction::triggered,
             [ this ]( auto ) { this->replaceSearch(); } );

    setSearchStartAction_ = new QAction( tr( "Set search start" ), this );
    connect( setSearchStartAction_, &QAction::triggered,
             [ this ]( auto ) { this->setSearchStart(); } );

    setSearchEndAction_ = new QAction( tr( "Set search end" ), this );
    connect( setSearchEndAction_, &QAction::triggered, [ this ]( auto ) { this->setSearchEnd(); } );

    clearSearchLimitAction_ = new QAction( tr( "Clear search limits" ), this );
    connect( clearSearchLimitAction_, &QAction::triggered,
             [ this ]( auto ) { this->clearSearchLimits(); } );

    setSelectionStartAction_ = new QAction( tr( "Set selection start" ), this );
    connect( setSelectionStartAction_, &QAction::triggered,
             [ this ]( auto ) { this->setSelectionStart(); } );

    setSelectionEndAction_ = new QAction( tr( "Set selection end" ), this );
    connect( setSelectionEndAction_, &QAction::triggered,
             [ this ]( auto ) { this->setSelectionEnd(); } );

    saveDefaultSplitterSizesAction_ = new QAction( tr( "Save splitter position" ), this );
    connect( saveDefaultSplitterSizesAction_, &QAction::triggered,
             [ this ]( auto ) { emit saveDefaultSplitterSizes(); } );

    popupMenu_ = new QMenu( this );
    highlightersMenu_ = popupMenu_->addMenu( "Highlighters" );
    popupMenu_->addSeparator();
    popupMenu_->addAction( markAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( copyAction_ );
    popupMenu_->addAction( saveToFileAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( findNextAction_ );
    popupMenu_->addAction( findPreviousAction_ );
    popupMenu_->addAction( addToSearchAction_ );
    popupMenu_->addAction( replaceSearchAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( setSearchStartAction_ );
    popupMenu_->addAction( setSearchEndAction_ );
    popupMenu_->addAction( clearSearchLimitAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( setSelectionStartAction_ );
    popupMenu_->addAction( setSelectionEndAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( saveDefaultSplitterSizesAction_ );
}

void AbstractLogView::considerMouseHovering( int x_pos, int y_pos )
{
    const auto line = convertCoordToLine( y_pos );
    if ( ( x_pos < leftMarginPx_ ) && ( line.has_value() ) && ( *line < logData_->getNbLine() ) ) {
        // Mouse moved in the margin, send event up
        // (possibly to highlight the overview)
        if ( line != lastHoveredLine_ ) {
            LOG_DEBUG << "Mouse moved in margin line: " << line;
            emit mouseHoveredOverLine( *line );
            lastHoveredLine_ = line;
        }
    }
    else {
        if ( lastHoveredLine_.has_value() ) {
            emit mouseLeftHoveringZone();
            lastHoveredLine_ = {};
        }
    }
}

void AbstractLogView::updateScrollBars()
{
    verticalScrollBar()->setRange( 0,
                                   qMax( 0, static_cast<int>( logData_->getNbLine().get()
                                                              - getNbVisibleLines().get() + 1 ) ) );

    const int hScrollMaxValue
        = qMax( 0, static_cast<int>( logData_->getMaxLength().get() ) - getNbVisibleCols() + 1 );

    horizontalScrollBar()->setRange( 0, hScrollMaxValue );
}

void AbstractLogView::drawTextArea( QPaintDevice* paint_device )
{
    // LOG_DEBUG << "devicePixelRatio: " << viewport()->devicePixelRatio();
    // LOG_DEBUG << "viewport size: " << viewport()->size().width();
    // LOG_DEBUG << "pixmap size: " << textPixmap.width();
    // Repaint the viewport
    QPainter painter( paint_device );
    // LOG_DEBUG << "font: " << viewport()->font().family().toStdString();
    // LOG_DEBUG << "font painter: " << painter.font().family().toStdString();

    painter.setFont( this->font() );
    painter.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing );

    const int fontHeight = charHeight_;
    const int fontAscent = painter.fontMetrics().ascent();
    const int nbCols = getNbVisibleCols();
    const int paintDeviceHeight = paint_device->height() / viewport()->devicePixelRatio();
    const int paintDeviceWidth = paint_device->width() / viewport()->devicePixelRatio();
    const QPalette& palette = viewport()->palette();
    const auto& highlighterSet = HighlighterSetCollection::get().currentSet();
    QColor foreColor, backColor;

    static const QBrush normalBulletBrush = QBrush( Qt::white );
    static const QBrush matchBulletBrush = QBrush( Qt::red );
    static const QBrush markBrush = QBrush( "dodgerblue" );
    static const QBrush markedMatchBrush = QBrush( "violet" );

    static constexpr int SEPARATOR_WIDTH = 1;
    static constexpr int BULLET_AREA_WIDTH = 11;
    static constexpr int CONTENT_MARGIN_WIDTH = 1;
    static constexpr int LINE_NUMBER_PADDING = 3;

    // First check the lines to be drawn are within range (might not be the case if
    // the file has just changed)
    const auto lines_in_file = logData_->getNbLine();

    if ( firstLine_ > lines_in_file )
        firstLine_ = LineNumber( lines_in_file.get() ? lines_in_file.get() - 1 : 0 );

    const auto nbLines
        = qMin( getNbVisibleLines(), lines_in_file - LinesCount( firstLine_.get() ) );

    const int bottomOfTextPx = static_cast<int>( nbLines.get() ) * fontHeight;

    LOG_DEBUG << "drawing lines from " << firstLine_ << " (" << nbLines << " lines)";
    LOG_DEBUG << "bottomOfTextPx: " << bottomOfTextPx;
    LOG_DEBUG << "Height: " << paintDeviceHeight;

    // First draw the bullet left margin
    painter.setPen( palette.color( QPalette::Text ) );
    painter.fillRect( 0, 0, BULLET_AREA_WIDTH, paintDeviceHeight, Qt::darkGray );

    // Column at which the content should start (pixels)
    int contentStartPosX = BULLET_AREA_WIDTH + SEPARATOR_WIDTH;

    // This is also the bullet zone width, used for marking clicks
    bulletZoneWidthPx_ = contentStartPosX;

    // Update the length of line numbers
    const int nbDigitsInLineNumber = countDigits( maxDisplayLineNumber().get() );

    // Draw the line numbers area
    int lineNumberAreaStartX = 0;
    if ( lineNumbersVisible_ ) {
        const auto lineNumberWidth = charWidth_ * nbDigitsInLineNumber;
        const auto lineNumberAreaWidth = 2 * LINE_NUMBER_PADDING + lineNumberWidth;
        lineNumberAreaStartX = contentStartPosX;

        painter.setPen( palette.color( QPalette::Text ) );
        painter.fillRect( contentStartPosX - SEPARATOR_WIDTH, 0,
                          lineNumberAreaWidth + SEPARATOR_WIDTH, paintDeviceHeight, Qt::darkGray );

        painter.drawLine( contentStartPosX + lineNumberAreaWidth - SEPARATOR_WIDTH, 0,
                          contentStartPosX + lineNumberAreaWidth - SEPARATOR_WIDTH,
                          paintDeviceHeight );

        // Update for drawing the actual text
        contentStartPosX += lineNumberAreaWidth;
    }
    else {
        painter.fillRect( contentStartPosX - SEPARATOR_WIDTH, 0, SEPARATOR_WIDTH + 1,
                          paintDeviceHeight, palette.color( QPalette::Disabled, QPalette::Text ) );
        // contentStartPosX += SEPARATOR_WIDTH;
    }

    painter.drawLine( BULLET_AREA_WIDTH, 0, BULLET_AREA_WIDTH, paintDeviceHeight - 1 );

    // This is the total width of the 'margin' (including line number if any)
    // used for mouse calculation etc...
    leftMarginPx_ = contentStartPosX + SEPARATOR_WIDTH;

    const auto searchStartIndex = lineIndex( searchStart_ );
    const auto searchEndIndex = [ this ] {
        auto index = lineIndex( searchEnd_ );
        if ( searchEnd_ + 1_lcount != displayLineNumber( index ) ) {
            // in filtered view lineIndex for "past the end" returns last line
            // it should not be marked as excluded
            index = index + 1_lcount;
        }

        return index;
    }();

    // Lines to write
    const auto expandedLines = logData_->getExpandedLines( firstLine_, nbLines );

    // Then draw each line
    for ( auto currentLine = 0_lcount; currentLine < nbLines; ++currentLine ) {
        const auto lineNumber = firstLine_ + currentLine;
        const QString logLine = logData_->getLineString( lineNumber );

        std::vector<HighlightedMatch> highlighterMatches;

        if ( selection_.isLineSelected( lineNumber ) ) {
            // Reverse the selected line
            foreColor = palette.color( QPalette::HighlightedText );
            backColor = palette.color( QPalette::Highlight );
            painter.setPen( palette.color( QPalette::Text ) );
        }
        else {
            const auto highlightType = highlighterSet.matchLine( logLine, highlighterMatches );

            if ( highlightType == HighlighterMatchType::LineMatch ) {
                // color applies to whole line
                foreColor = highlighterMatches.front().foreColor();
                backColor = highlighterMatches.front().backColor();
            }
            else {
                // Use the default colors
                if ( lineNumber < searchStartIndex || lineNumber >= searchEndIndex ) {
                    foreColor = palette.brush( QPalette::Disabled, QPalette::Text ).color();
                }
                else {
                    foreColor = palette.color( QPalette::Text );
                }

                backColor = palette.color( QPalette::Base );
            }
        }

        std::vector<HighlightedMatch> allHighlights;
        allHighlights.reserve( highlighterMatches.size() );
        std::transform( highlighterMatches.begin(), highlighterMatches.end(),
                        std::back_inserter( allHighlights ), [ &logLine ]( const auto& match ) {
                            const auto prefix = logLine.leftRef( match.startColumn() );
                            const auto expandedPrefixLength = untabify( prefix ).length();
                            auto startDelta = expandedPrefixLength - prefix.length();

                            const auto matchPart
                                = logLine.midRef( match.startColumn(), match.length() );
                            const auto expandedMatchLength
                                = untabify( matchPart, expandedPrefixLength ).length();
                            auto lengthDelta = expandedMatchLength - matchPart.length();

                            return HighlightedMatch{ match.startColumn() + startDelta,
                                                     match.length() + lengthDelta,
                                                     match.foreColor(), match.backColor() };
                        } );

        // string to print, cut to fit the length and position of the view
        const QString expandedLine = expandedLines[ currentLine.get() ];
        const QString cutLine = expandedLine.mid( firstCol_, nbCols );

        // Position in pixel of the base line of the line to print
        const int yPos = static_cast<int>( currentLine.get() ) * fontHeight;
        const int xPos = contentStartPosX + CONTENT_MARGIN_WIDTH;

        // Has the line got elements to be highlighted
        std::vector<HighlightedMatch> quickFindMatches;
        quickFindPattern_->matchLine( expandedLine, quickFindMatches );
        allHighlights.insert( allHighlights.end(),
                              std::make_move_iterator( quickFindMatches.begin() ),
                              std::make_move_iterator( quickFindMatches.end() ) );

        // Is there something selected in the line?
        const auto selectionPortion = selection_.getPortionForLine( lineNumber );
        if ( selectionPortion.isValid() ) {
            allHighlights.emplace_back( selectionPortion.startColumn(), selectionPortion.length(),
                                        palette.color( QPalette::HighlightedText ),
                                        palette.color( QPalette::Highlight ) );
        }

        if ( !allHighlights.empty() ) {
            // We use the LineDrawer and its chunks because the
            // line has to be somehow highlighted
            LineDrawer lineDrawer( backColor );

            auto foreColors = std::vector<QColor>( static_cast<size_t>( nbCols + 1 ), foreColor );
            auto backColors = std::vector<QColor>( static_cast<size_t>( nbCols + 1 ), backColor );

            for ( const auto& match : allHighlights ) {
                const auto start = match.startColumn() - firstCol_;
                const auto end = start + match.length();

                // Ignore matches that are *completely* outside view area
                if ( ( start < 0 && end < 0 ) || start >= nbCols )
                    continue;

                const auto firstCol_umn = static_cast<size_t>( qMax( start, 0 ) );
                const auto lastColumn
                    = static_cast<size_t>( qMin( start + match.length(), nbCols ) );

                for ( auto column = firstCol_umn; column < lastColumn; ++column ) {
                    foreColors[ column ] = match.foreColor();
                    backColors[ column ] = match.backColor();
                }
            }

            std::vector<LineChunk> highlightChunks;
            auto lastMatchStart = 0;
            for ( auto column = 0u; column < foreColors.size() - 1; ++column ) {
                if ( foreColors[ column ] != foreColors[ column + 1 ]
                     || backColors[ column ] != backColors[ column + 1 ] ) {
                    lineDrawer.addChunk( { lastMatchStart, static_cast<int>( column ),
                                           foreColors[ column ], backColors[ column ] } );
                    lastMatchStart = static_cast<int>( column + 1 );
                }
            }
            if ( lastMatchStart < nbCols ) {
                lineDrawer.addChunk(
                    { lastMatchStart, nbCols, foreColors.back(), backColors.back() } );
            }

            lineDrawer.draw( painter, xPos, yPos, viewport()->width(), cutLine,
                             CONTENT_MARGIN_WIDTH );
        }
        else {
            // Nothing to be highlighted, we print the whole line!
            painter.fillRect( xPos - CONTENT_MARGIN_WIDTH, yPos, viewport()->width(), fontHeight,
                              backColor );
            // (the rectangle is extended on the left to cover the small
            // margin, it looks better (LineDrawer does the same) )
            painter.setPen( foreColor );
            painter.drawText( xPos, yPos + fontAscent, cutLine );
        }

        // Then draw the bullet
        painter.setPen( Qt::black );
        const int circleSize = 3;
        const int arrowHeight = 4;
        const int middleXLine = BULLET_AREA_WIDTH / 2;
        const int middleYLine = yPos + ( fontHeight / 2 );

        using LineTypeFlags = AbstractLogData::LineTypeFlags;
        const auto line_type = lineType( lineNumber );
        if ( line_type.testFlag( LineTypeFlags::Mark ) ) {
            // A pretty arrow if the line is marked
            const QPointF points[ 7 ] = {
                QPointF( 1, middleYLine - 2 ),
                QPointF( middleXLine, middleYLine - 2 ),
                QPointF( middleXLine, middleYLine - arrowHeight ),
                QPointF( BULLET_AREA_WIDTH - 1, middleYLine ),
                QPointF( middleXLine, middleYLine + arrowHeight ),
                QPointF( middleXLine, middleYLine + 2 ),
                QPointF( 1, middleYLine + 2 ),
            };

            painter.setBrush( line_type.testFlag( LineTypeFlags::Match ) ? markedMatchBrush
                                                                         : markBrush );
            painter.drawPolygon( points, 7 );
        }
        else {
            // For pretty circles
            painter.setRenderHint( QPainter::Antialiasing );

            QBrush brush = normalBulletBrush;
            if ( line_type.testFlag( LineTypeFlags::Match ) )
                brush = matchBulletBrush;
            painter.setBrush( brush );
            painter.drawEllipse( middleXLine - circleSize, middleYLine - circleSize, circleSize * 2,
                                 circleSize * 2 );
        }

        // Draw the line number
        if ( lineNumbersVisible_ ) {
            static const QString lineNumberFormat( "%1" );
            const QString& lineNumberStr = lineNumberFormat.arg(
                displayLineNumber( lineNumber ).get(), nbDigitsInLineNumber );
            painter.setPen( Qt::white );
            painter.drawText( lineNumberAreaStartX + LINE_NUMBER_PADDING, yPos + fontAscent,
                              lineNumberStr );
        }
    } // For each line

    if ( bottomOfTextPx < paintDeviceHeight ) {
        // The lines don't cover the whole device
        painter.fillRect( contentStartPosX, bottomOfTextPx, paintDeviceWidth - contentStartPosX,
                          paintDeviceHeight, palette.color( QPalette::Window ) );
    }
}

// Draw the "pull to follow" bar and return a pixmap.
// The width is passed in "logic" pixels.
QPixmap AbstractLogView::drawPullToFollowBar( int width, qreal pixel_ratio )
{
    static constexpr int barWidth = 40;
    QPixmap pixmap( static_cast<int>( width * pixel_ratio ), static_cast<int>( barWidth * 6.0 ) );
    pixmap.setDevicePixelRatio( pixel_ratio );
    pixmap.fill( this->palette().color( this->backgroundRole() ) );
    const int nbBars = width / ( barWidth * 2 ) + 1;

    QPainter painter( &pixmap );
    painter.setPen( QPen( QColor( 0, 0, 0, 0 ) ) );
    painter.setBrush( QBrush( QColor( "lightyellow" ) ) );

    for ( int i = 0; i < nbBars; ++i ) {
        QPoint points[ 4 ] = { { ( i * 2 + 1 ) * barWidth, 0 },
                               { 0, ( i * 2 + 1 ) * barWidth },
                               { 0, ( i + 1 ) * 2 * barWidth },
                               { ( i + 1 ) * 2 * barWidth, 0 } };
        painter.drawConvexPolygon( points, 4 );
    }

    return pixmap;
}

void AbstractLogView::disableFollow()
{
    emit followModeChanged( false );
    followElasticHook_.hook( false );
}

void AbstractLogView::setHighlighterSet( QAction* action )
{
    saveCurrentHighlighterFromAction( action );
    textAreaCache_.invalid_ = true;
    update();
}

namespace {

// Convert the length of the pull to follow bar to pixels
int mapPullToFollowLength( int length )
{
    return length / 14;
}

} // namespace
