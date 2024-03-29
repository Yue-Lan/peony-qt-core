#include "file-info.h"

#include "file-info-manager.h"
#include "file-info-job.h"

#include <QDebug>

using namespace Peony;

FileInfo::FileInfo(QObject *parent) : QObject (parent)
{
    m_cancellable = g_cancellable_new();
}

FileInfo::~FileInfo()
{
    //qDebug()<<"~FileInfo";
    FileInfoManager *info_manager = FileInfoManager::getInstance();
    info_manager->removeFileInfobyUri(m_uri);

    g_object_unref(m_cancellable);
}

std::shared_ptr<FileInfo> FileInfo::fromUri(QString uri)
{
    FileInfoManager *info_manager = FileInfoManager::getInstance();
    std::shared_ptr<FileInfo> info = info_manager->findFileInfoByUri(uri);
    if (info != nullptr) {
        return info;
    } else {
        std::shared_ptr<FileInfo> newly_info = std::make_shared<FileInfo>();
        newly_info->m_uri = uri;
        newly_info->m_file = g_file_new_for_uri(uri.toUtf8());
        newly_info->m_parent = g_file_get_parent(newly_info->m_file);
        newly_info->m_is_remote = !g_file_is_native(newly_info->m_file);
        GFileType type = g_file_query_file_type(newly_info->m_file,
                                                G_FILE_QUERY_INFO_NONE,
                                                nullptr);
        switch (type) {
        case G_FILE_TYPE_DIRECTORY:
            //qDebug()<<"dir";
            newly_info->m_is_dir = true;
            break;
        case G_FILE_TYPE_MOUNTABLE:
            //qDebug()<<"mountable";
            newly_info->m_is_volume = true;
            break;
        default:
            break;
        }
        info_manager->insertFileInfo(newly_info);
        return newly_info;
    }
}

std::shared_ptr<FileInfo> FileInfo::fromPath(QString path)
{
    QString uri = "file://"+path;
    return fromUri(uri);
}

std::shared_ptr<FileInfo> FileInfo::fromGFile(GFile *file)
{
    char *uri_str = g_file_get_uri(file);
    QString uri = uri_str;
    g_free(uri_str);
    return fromUri(uri);
}
