#include "file-info-job.h"

#include "file-info.h"

#include "file-info-manager.h"

#include <QDebug>
#include <QDateTime>

using namespace Peony;

FileInfoJob::FileInfoJob(std::shared_ptr<FileInfo> info, QObject *parent) : QObject(parent)
{
    m_info = info;
    m_cancellble = info->m_cancellable;
}

/*!
 * \brief FileInfoJob::~FileInfoJob
 * <br>
 * For FileInfo having a handle shared by all classes instance,
 * the shared data first is created in a global hash hold by
 * Peony::FileInfoManager instance. This makes shared_ptr
 * ref count always more than 1.
 * </br>
 * <br>
 * At this class we aslo hold a shared data from global hash
 * (though we aslo change its content here).
 * When info job instance in destructor, it means shared data
 * use count will reduce 1 soon. If current use count is 2,
 * that means there won't be another holder expect global hash.
 * So we can remove the last shared_ptr in the hash, then FileInfo
 * instance will be removed automaticly.
 * </br>
 * \see FileInfo::~FileInfo(), FileEnumerator::~FileEnumerator().
 */
FileInfoJob::~FileInfoJob()
{
    //qDebug()<<"~Job"<<m_info.use_count();
    if (m_info.use_count() <= 2) {
        Peony::FileInfoManager *mgr = Peony::FileInfoManager::getInstance();
        mgr->remove(m_info);
    }
}

void FileInfoJob::cancel()
{
    //NOTE: do not use same cancellble for cancelling, otherwise all job might be cancelled.
    g_cancellable_cancel(m_cancellble);
    g_object_unref(m_info->m_cancellable);
    m_info->m_cancellable = g_cancellable_new();
    m_cancellble = m_info->m_cancellable;
}

bool FileInfoJob::querySync()
{
    FileInfo *info = nullptr;
    if (auto data = m_info.get()) {
        info = data;
    } else {
        if (m_auto_delete)
            deleteLater();
        return false;
    }
    GError *err = nullptr;
    if (info->m_file_info) {
        g_object_unref(info->m_file_info);
    }
    info->m_file_info = g_file_query_info(info->m_file,
                                          "standard::*," G_FILE_ATTRIBUTE_TIME_MODIFIED G_FILE_ATTRIBUTE_ID_FILE,
                                          G_FILE_QUERY_INFO_NONE,
                                          nullptr,
                                          &err);

    if (err) {
        qDebug()<<err->code<<err->message;
        g_error_free(err);
        if (m_auto_delete)
            deleteLater();
        return false;
    }

    refreshInfoContents();
    if (m_auto_delete)
        deleteLater();
    return true;
}

GAsyncReadyCallback FileInfoJob::query_info_async_callback(GFile *file, GAsyncResult *res, FileInfoJob *thisJob)
{
    FileInfo *info = nullptr;
    std::weak_ptr<FileInfo> wk = thisJob->m_info;

    if (wk.lock()) {
        info = wk.lock().get();
    } else {
        Q_EMIT thisJob->queryAsyncFinished(false);
        return nullptr;
    }

    GError *err = nullptr;
    if (info->m_file_info)
        g_object_unref(info->m_file_info);
    info->m_file_info = g_file_query_info_finish(file,
                                                 res,
                                                 &err);
    if (err) {
        qDebug()<<err->code<<err->message;
        g_error_free(err);
        Q_EMIT thisJob->queryAsyncFinished(false);
        return nullptr;
    }

    thisJob->refreshInfoContents();
    Q_EMIT thisJob->queryAsyncFinished(true);
    return nullptr;
}

void FileInfoJob::queryAsync()
{
    FileInfo *info = nullptr;
    if (auto data = m_info) {
        info = data.get();
        cancel();
    } else {
        Q_EMIT queryAsyncFinished(false);
        return;
    }
    g_file_query_info_async(info->m_file,
                            "standard::*," G_FILE_ATTRIBUTE_TIME_MODIFIED G_FILE_ATTRIBUTE_ID_FILE,
                            G_FILE_QUERY_INFO_NONE,
                            G_PRIORITY_DEFAULT,
                            m_cancellble,
                            GAsyncReadyCallback(query_info_async_callback),
                            this);

    if (m_auto_delete)
        connect(this, &FileInfoJob::queryAsyncFinished, this, &FileInfoJob::deleteLater, Qt::QueuedConnection);
}

void FileInfoJob::refreshInfoContents()
{
    FileInfo *info = nullptr;
    if (auto data = m_info) {
        info = data.get();
    } else {
        return;
    }
    GFileType type = g_file_info_get_file_type (info->m_file_info);
    switch (type) {
    case G_FILE_TYPE_DIRECTORY:
        //qDebug()<<"dir";
        info->m_is_dir = true;
        break;
    case G_FILE_TYPE_MOUNTABLE:
        //qDebug()<<"mountable";
        info->m_is_volume = true;
        break;
    default:
        break;
    }
    info->m_display_name = QString (g_file_info_get_display_name(info->m_file_info));
    GIcon *g_icon = g_file_info_get_icon (info->m_file_info);
    const gchar* const* icon_names = g_themed_icon_get_names(G_THEMED_ICON (g_icon));
    if (icon_names)
        info->m_icon_name = QString (*icon_names);
    //qDebug()<<m_display_name<<m_icon_name;
    info->m_file_id = g_file_info_get_attribute_string(info->m_file_info, G_FILE_ATTRIBUTE_ID_FILE);

    info->m_content_type = g_file_info_get_content_type (info->m_file_info);
    info->m_size = g_file_info_get_attribute_uint64(info->m_file_info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
    info->m_modified_time = g_file_info_get_attribute_uint64(info->m_file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);

    char *content_type = g_content_type_get_description (g_file_info_get_content_type (info->m_file_info));
    info->m_file_type = content_type;
    g_free (content_type);
    content_type = nullptr;

    info->m_file_size = g_format_size_full(info->m_size, G_FORMAT_SIZE_LONG_FORMAT);

    QDateTime date = QDateTime::fromMSecsSinceEpoch(info->m_modified_time*1000);
    info->m_modified_date = date.toString(Qt::SystemLocaleShortDate);

    Q_EMIT info->updated();
}
