/*
 * Copyright (C) 2009, 2010, 2014, 2015 Nicolas Bonnefon and other contributors
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

#ifndef LOGDATAWORKERTHREAD_H
#define LOGDATAWORKERTHREAD_H

#include <oneapi/tbb/task_group.h>
#include <variant>


#include <QObject>
#include <QTextCodec>

#include <tbb/task_group.h>

#include "atomicflag.h"
#include "filedigest.h"
#include "synchronization.h"

#include "encodingdetector.h"
#include "linepositionarray.h"
#include "loadingstatus.h"

struct IndexedHash {
    qint64 size = 0;
    quint64 fullDigest = 0;

    qint64 headerSize = 0;
    quint64 headerDigest = 0;

    qint64 tailSize = 0;
    qint64 tailOffset = 0;
    quint64 tailDigest = 0;
};

template <typename Data, typename LockGuard>
class IndexingDataAccessor {
  public:
    IndexingDataAccessor( Data data )
        : data_( data )
        , guard_( data->dataMutex_ )
    {
    }

    ~IndexingDataAccessor() = default;

    qint64 getIndexedSize() const
    {
        return data_->getIndexedSize();
    }

    IndexedHash getHash() const
    {
        return data_->getHash();
    }

    // Get the length of the longest line
    LineLength getMaxLength() const
    {
        return data_->getMaxLength();
    }

    // Get the total number of lines
    LinesCount getNbLines() const
    {
        return data_->getNbLines();
    }

    // Get the position (in byte from the beginning of the file)
    // of the end of the passed line.
    LineOffset getPosForLine( LineNumber line ) const
    {
        return data_->getPosForLine( line );
    }

    // Get the guessed encoding for the content.
    QTextCodec* getEncodingGuess() const
    {
        return data_->getEncodingGuess();
    }
    void setEncodingGuess( QTextCodec* codec )
    {
        data_->setEncodingGuess( codec );
    }

    QTextCodec* getForcedEncoding() const
    {
        return data_->getForcedEncoding();
    }
    void forceEncoding( QTextCodec* codec )
    {
        return data_->forceEncoding( codec );
    }

    // Atomically add to all the existing
    // indexing data.
    void addAll( const QByteArray& block, LineLength length,
                 const FastLinePositionArray& linePosition, QTextCodec* encoding )
    {
        data_->addAll( block, length, linePosition, encoding );
    }

    void setHeaderHash( quint64 digest, qint64 size )
    {
        data_->hash_.headerSize = size;
        data_->hash_.headerDigest = digest;
    }

    void setTailHash( quint64 digest, qint64 offset, qint64 size )
    {
        data_->hash_.tailSize = size;
        data_->hash_.tailOffset = offset;
        data_->hash_.tailDigest = digest;
    }

    // Completely clear the indexing data.
    void clear()
    {
        data_->clear();
    }

    size_t allocatedSize() const
    {
        return data_->allocatedSize();
    }

  private:
    Data data_;
    LockGuard guard_;
};

// This class is a thread-safe set of indexing data.
class IndexingData {
  public:
    using ConstAccessor = IndexingDataAccessor<const IndexingData*, ScopedLock>;
    using MutateAccessor = IndexingDataAccessor<IndexingData*, ScopedLock>;

  private:
    qint64 getIndexedSize() const;

    IndexedHash getHash() const;

    // Get the length of the longest line
    LineLength getMaxLength() const;

    // Get the total number of lines
    LinesCount getNbLines() const;

    // Get the position (in byte from the beginning of the file)
    // of the end of the passed line.
    LineOffset getPosForLine( LineNumber line ) const;

    // Get the guessed encoding for the content.
    QTextCodec* getEncodingGuess() const;
    void setEncodingGuess( QTextCodec* codec );

    QTextCodec* getForcedEncoding() const;
    void forceEncoding( QTextCodec* codec );

    // Atomically add to all the existing
    // indexing data.
    void addAll( const QByteArray& block, LineLength length,
                 const FastLinePositionArray& linePosition, QTextCodec* encoding );

    // Completely clear the indexing data.
    void clear();

    size_t allocatedSize() const;

  private:
    mutable Mutex dataMutex_;

    LinePositionArray linePosition_;
    LineLength maxLength_;

    FileDigest hashBuilder_;
    IndexedHash hash_;

    QTextCodec* encodingGuess_{};
    QTextCodec* encodingForced_{};

    friend ConstAccessor;
    friend MutateAccessor;
};

struct IndexingState {

    EncodingParameters encodingParams;
    LineOffset::UnderlyingType pos{};
    int64_t max_length{};
    LineLength::UnderlyingType additional_spaces{};
    LineOffset::UnderlyingType end{};
    LineOffset::UnderlyingType file_size{};

    QTextCodec* encodingGuess{};
    QTextCodec* fileTextCodec{};
};

using OperationResult = std::variant<bool, MonitoredFileStatus>;

class IndexOperation : public QObject {
    Q_OBJECT
  public:
    IndexOperation( const QString& fileName, const std::shared_ptr<IndexingData>& indexingData,
                    AtomicFlag& interruptRequest )
        : fileName_( fileName )
        , indexing_data_( indexingData )
        , interruptRequest_( interruptRequest )
    {
    }

    // Run the indexing operation, returns true if it has been done
    // and false if it has been cancelled (results not copied)
    virtual OperationResult run() = 0;

  signals:
    void indexingProgressed( int );
    void indexingFinished( bool );
    void fileCheckFinished( MonitoredFileStatus );

  protected:
    // Returns the total size indexed
    // Modify the passed linePosition and maxLength
    void doIndex( LineOffset initialPosition );

    QString fileName_;
    std::shared_ptr<IndexingData> indexing_data_;
    AtomicFlag& interruptRequest_;

  private:
    FastLinePositionArray parseDataBlock( LineOffset::UnderlyingType blockBegining,
                                          const QByteArray& block, IndexingState& state ) const;

    void guessEncoding( const QByteArray& block, IndexingData::MutateAccessor& scopedAccessor,
                        IndexingState& state ) const;
};

class FullIndexOperation : public IndexOperation {
    Q_OBJECT
  public:
    FullIndexOperation( const QString& fileName, const std::shared_ptr<IndexingData>& indexingData,
                        AtomicFlag& interruptRequest, QTextCodec* forcedEncoding = nullptr )
        : IndexOperation( fileName, indexingData, interruptRequest )
        , forcedEncoding_( forcedEncoding )
    {
    }
    OperationResult run() override;

  private:
    QTextCodec* forcedEncoding_;
};

class PartialIndexOperation : public IndexOperation {
    Q_OBJECT
  public:
    PartialIndexOperation( const QString& fileName,
                           const std::shared_ptr<IndexingData>& indexingData,
                           AtomicFlag& interruptRequest )
        : IndexOperation( fileName, indexingData, interruptRequest )
    {
    }

    OperationResult run() override;
};

class CheckFileChangesOperation : public IndexOperation {
    Q_OBJECT
  public:
    CheckFileChangesOperation( const QString& fileName,
                               const std::shared_ptr<IndexingData>& indexingData,
                               AtomicFlag& interruptRequest )
        : IndexOperation( fileName, indexingData, interruptRequest )
    {
    }

    OperationResult run() override;

  private:
    MonitoredFileStatus doCheckFileChanges();
};

class LogDataWorker : public QObject {
    Q_OBJECT

  public:
    // Pass a pointer to the IndexingData (initially empty)
    // This object will change it when indexing (IndexingData must be thread safe!)
    explicit LogDataWorker( const std::shared_ptr<IndexingData>& indexing_data );
    ~LogDataWorker() noexcept override;

    LogDataWorker( const LogDataWorker& ) = delete;
    LogDataWorker& operator=( const LogDataWorker&& ) = delete;

    LogDataWorker( LogDataWorker&& ) = delete;
    LogDataWorker& operator=( LogDataWorker&& ) = delete;

    // Attaches to a file on disk. Attaching to a non existant file
    // will work, it will just appear as an empty file.
    void attachFile( const QString& fileName );
    // Instructs the thread to start a new full indexing of the file, sending
    // signals as it progresses.
    void indexAll( QTextCodec* forcedEncoding = nullptr );
    // Instructs the thread to start a partial indexing (starting at
    // the end of the file as indexed).
    void indexAdditionalLines();

    void checkFileChanges();

    // Interrupts the indexing if one is in progress
    void interrupt();

  signals:
    // Sent during the indexing process to signal progress
    // percent being the percentage of completion.
    void indexingProgressed( int percent );
    // Sent when indexing is finished, signals the client
    // to copy the new data back.
    void indexingFinished( LoadingStatus status );

    // Sent when check file is finished, signals the client
    // to copy the new data back.
    void checkFileChangesFinished( MonitoredFileStatus status );

  private slots:
    void onIndexingFinished( bool result );
    void onCheckFileFinished( MonitoredFileStatus result );

  private:
    OperationResult connectSignalsAndRun( IndexOperation* operationRequested );
    void waitForDone();

    tbb::task_group operationsExecuter_;

    AtomicFlag interruptRequest_;

    // Mutex to protect operationRequested_ and friends
    Mutex mutex_;
    QString fileName_;

    // Pointer to the owner's indexing data (we modify it)
    std::shared_ptr<IndexingData> indexing_data_;
};

#endif
