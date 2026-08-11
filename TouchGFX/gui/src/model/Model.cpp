#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

Model::Model() : modelListener(0), lastRevision(0), lastLibraryRevision(0)
{

}

void Model::tick()
{
    MediaSnapshot snapshot;
    MediaPlayer_GetSnapshot(&snapshot);
    if (modelListener != 0 && snapshot.libraryRevision != lastLibraryRevision)
    {
        lastLibraryRevision = snapshot.libraryRevision;
        modelListener->mediaLibraryChanged();
    }
    if (modelListener != 0 && snapshot.revision != lastRevision)
    {
        lastRevision = snapshot.revision;
        modelListener->mediaStateChanged(snapshot);
    }
}
