/*
Copyright 2011 Frans van den Bergh. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

THIS SOFTWARE IS PROVIDED BY Frans van den Bergh ''AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Frans van den Bergh OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the
authors and should not be interpreted as representing official policies, either expressed
or implied, of the Council for Scientific and Industrial Research (CSIR).
*/
#include "worker_thread.h"
#include "worker_thread.moc"
#include <string>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include <QFileInfo>
#include <QSharedPointer>
#include "mtfmapper_app.h"

using std::cout;
using std::endl;
using std::string;

Worker_thread::Worker_thread(QWidget* parent) : parent((mtfmapper_app*)parent) {

}

void Worker_thread::set_files(const QStringList& files) {
    input_files = files;
}

void Worker_thread::run(void) {
    output_files.clear();
    for (int i=0; i < input_files.size(); i++) {
        emit send_progress_indicator(i);
        QString tempdir = tr("%1/mtfmappertemp_%2").arg(QDir::tempPath()).arg(i);
        QDir().mkdir(tempdir);
        char* buffer = new char[4096];
        sprintf(buffer, "./mtf_mapper %s %s %s", 
            input_files.at(i).toLocal8Bit().constData(),
            tempdir.toLocal8Bit().constData(),
            arguments.toLocal8Bit().constData()
        );
        cout << "Processing file " << input_files.at(i).toLocal8Bit().constData() << ":" 
             << arguments.toLocal8Bit().constData() << endl;
        int rval = system(buffer);
        
        if (rval >= 0) {

            // this call must come from within the worker thread, since we
            // may have to perform a raw conversion in the worker thread
            // which would cause the display image filename (root of each data set object)
            // to differ from the file containing the exif info
            emit send_exif_filename(input_files.at(i), tempdir);
            
            QString fname(QFileInfo(input_files.at(i)).baseName());
            emit send_parent_item(fname, input_files.at(i));
            
            QString an_file = QString("%1/annotated.png").arg(tempdir);
            if (QFile().exists(an_file)) {
                emit send_child_item(QString("annotated"), an_file);
                emit send_delete_item(an_file);
            }
            QString pr_file = QString("%1/profile_image.png").arg(tempdir);
            if (QFile().exists(pr_file)) {
                emit send_child_item(QString("profile"), pr_file);
                emit send_delete_item(pr_file);
                emit send_delete_item(tempdir + QString("/profile.gnuplot"));
                emit send_delete_item(tempdir + QString("/profile.txt"));
                emit send_delete_item(tempdir + QString("/profile_peak.txt"));
            }
            QString gi_file = QString("%1/grid_image.png").arg(tempdir);
            if (QFile().exists(gi_file)) {
                emit send_child_item(QString("grid2d"), gi_file);
                emit send_delete_item(gi_file);
                emit send_delete_item(tempdir + QString("/grid.gnuplot"));
                emit send_delete_item(tempdir + QString("/grid.txt"));
            }
            QString gs_file = QString("%1/grid_surface.png").arg(tempdir);
            if (QFile().exists(gs_file)) {
                emit send_child_item(QString("grid3d"), gs_file);
                emit send_delete_item(gs_file);
            }
            emit send_close_item();
        }
        delete [] buffer;
    }
    emit send_progress_indicator(input_files.size());
}

void Worker_thread::receive_arg_string(QString s) {
    arguments = s;
}
