/*
 * Copyright (C) 2009, 2010, 2013, 2014 Nicolas Bonnefon and other contributors
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

#ifndef LOGDATA_H
#define LOGDATA_H

#include <memory>

#include <QObject>
#include <QString>
#include <QFile>
#include <QVector>
#include <QMutex>
#include <QDateTime>

#include "utils.h"

#include "abstractlogdata.h"
#include "logdataworkerthread.h"
#include "filewatcher.h"
#include "loadingstatus.h"

class LogFilteredData;

// Thrown when trying to attach an already attached LogData
class CantReattachErr {};

// Represents a complete set of data to be displayed (ie. a log file content)
// This class is thread-safe.
class LogData : public AbstractLogData {
  Q_OBJECT

  public:
    // Creates an empty LogData
    LogData();
    // Destroy an object
    ~LogData();

    enum MonitoredFileStatus { Unchanged, DataAdded, Truncated };

    // Attaches the LogData to a file on disk
    // It starts the asynchronous indexing and returns (almost) immediately
    // Attaching to a non existant file works and the file is reported
    // to be empty.
    // Reattaching is forbidden and will throw.
    void attachFile( const QString& fileName );
    // Interrupt the loading and report a null file.
    // Does nothing if no loading in progress.
    void interruptLoading();
    // Creates a new filtered data.
    // ownership is passed to the caller
    LogFilteredData* getNewFilteredData() const;
    // Returns the size if the file in bytes
    qint64 getFileSize() const;
    // Returns the last modification date for the file.
    // Null if the file is not on disk.
    QDateTime getLastModifiedDate() const;
    // Throw away all the file data and reload/reindex.
    void reload();

  signals:
    // Sent during the 'attach' process to signal progress
    // percent being the percentage of completion.
    void loadingProgressed( int percent );
    // Signal the client the file is fully loaded and available.
    void loadingFinished( LoadingStatus status );
    // Sent when the file on disk has changed, will be followed
    // by loadingProgressed if needed and then a loadingFinished.
    void fileChanged( LogData::MonitoredFileStatus status );

  private slots:
    // Consider reloading the file when it changes on disk updated
    void fileChangedOnDisk();
    // Called when the worker thread signals the current operation ended
    void indexingFinished( LoadingStatus status );

  private:
    // This class models an indexing operation.
    // It exists to permit LogData to delay the operation if another
    // one is ongoing (operations are asynchronous)
    class LogDataOperation {
      public:
        LogDataOperation( const QString& fileName ) : filename_( fileName ) {}
        // Permit each child to have its destructor
        virtual ~LogDataOperation() {};

        void start( LogDataWorkerThread& workerThread ) const
        { doStart( workerThread ); }
        const QString& getFilename() const { return filename_; }
        virtual bool isFull() const { return true; }

      protected:
        virtual void doStart( LogDataWorkerThread& workerThread ) const = 0;
        QString filename_;
    };

    // Attaching a new file (change name + full index)
    class AttachOperation : public LogDataOperation {
      public:
        AttachOperation( const QString& fileName )
            : LogDataOperation( fileName ) {}
        ~AttachOperation() {};

        bool isFull() const { return true; }

      protected:
        void doStart( LogDataWorkerThread& workerThread ) const;
    };

    // Reindexing the current file
    class FullIndexOperation : public LogDataOperation {
      public:
        FullIndexOperation() : LogDataOperation( QString() ) {}
        ~FullIndexOperation() {};

        bool isFull() const { return false; }

      protected:
        void doStart( LogDataWorkerThread& workerThread ) const;
    };

    // Indexing part of the current file (from fileSize)
    class PartialIndexOperation : public LogDataOperation {
      public:
        PartialIndexOperation( qint64 fileSize )
            : LogDataOperation( QString() ), filesize_( fileSize ) {}
        ~PartialIndexOperation() {};

        bool isFull() const { return false; }

      protected:
        void doStart( LogDataWorkerThread& workerThread ) const;

      private:
        qint64 filesize_;
    };

    std::shared_ptr<FileWatcher> fileWatcher_;
    MonitoredFileStatus fileChangedOnDisk_;

    // Implementation of virtual functions
    QString doGetLineString( qint64 line ) const override;
    QString doGetExpandedLineString( qint64 line ) const override;
    QStringList doGetLines( qint64 first, int number ) const override;
    QStringList doGetExpandedLines( qint64 first, int number ) const override;
    qint64 doGetNbLine() const override;
    int doGetMaxLength() const override;
    int doGetLineLength( qint64 line ) const override;

    void enqueueOperation( std::shared_ptr<const LogDataOperation> newOperation );
    void startOperation();

    QString indexingFileName_;
    std::unique_ptr<QFile> attached_file_;
    LinePositionArray linePosition_;
    qint64 fileSize_;
    qint64 nbLines_;
    int maxLength_;
    QDateTime lastModifiedDate_;
    std::shared_ptr<const LogDataOperation> currentOperation_;
    std::shared_ptr<const LogDataOperation> nextOperation_;

    // To protect the file:
    mutable QMutex fileMutex_;
    // To protect linePosition_, fileSize_ and maxLength_:
    mutable QMutex dataMutex_;
    // (are mutable to allow 'const' function to touch it,
    // while remaining const)
    // When acquiring both, data should be help before locking file.

    LogDataWorkerThread workerThread_;
};

Q_DECLARE_METATYPE( LogData::MonitoredFileStatus );

#endif
