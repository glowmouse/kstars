/***************************************************************************
                          deepskycomponent.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : 2005/15/08
    copyright            : (C) 2005 by Thomas Kabelmann
    email                : thomas.kabelmann@gmx.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "deepskycomponent.h"

#include <QPainter>
#include <QDir>
#include <QFile>

#include <klocale.h>
#include <kstandarddirs.h>

#include "skyobjects/deepskyobject.h"
#include "dms.h"
#include "ksfilereader.h"
#include "kstarsdata.h"
#include "skymap.h"
#include "skylabel.h"
#include "skylabeler.h"
#include "Options.h"
#include "skymesh.h"
#include "kstarsdb.h"

DeepSkyComponent::DeepSkyComponent( SkyComposite *parent ) :
    SkyComponent(parent)
{
    m_skyMesh = SkyMesh::Instance();
    // Add labels
    for( int i = 0; i <= MAX_LINENUMBER_MAG; i++ )
        m_labelList[ i ] = new LabelList;
    loadData();
}

DeepSkyComponent::~DeepSkyComponent()
{
    clear();
}

bool DeepSkyComponent::selected()
{
    return Options::showDeepSky();
}

void DeepSkyComponent::update( KSNumbers* )
{}


void DeepSkyComponent::loadData()
{

/*
    KStarsDB *ksdb = KStarsDB::Create();
    ksdb->createDefaultDatabase("data/kstars.db");
    ksdb->migrateData("ngcic.dat");

    exit(1);
*/
    KStarsData* data = KStarsData::Instance();

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("data/kstars.db");
    db.open();

    QSqlQuery query(db);
    QSqlQuery dsoquery(db), tmpq(db);

    query.exec("SELECT *, rowid FROM dso");
    
    while ( query.next() ) {
        QString line, con, ss, name[2], longname;
        QString cat[2], sgn;
        float mag = query.value(7).toString().toFloat(), ras = query.value(2).toString().toFloat(), a = query.value(11).toString().toFloat(), b = query.value(10).toString().toFloat();
        int type = query.value(8).toString().toInt();
        int ingc = 3, imess(-1), rah = query.value(0).toString().toFloat(), ram = query.value(1).toString().toInt();
        int dd = query.value(4).toString().toInt(), dm = query.value(5).toString().toInt(), ds = query.value(6).toString().toInt(), pa = query.value(9).toString().toInt();
        int pgc = 0, ugc = 0, k = 0;
        QChar iflag;

        dsoquery.prepare("SELECT designation, idCTG FROM od WHERE idDSO = :iddso");
        dsoquery.bindValue(":iddso", query.value(13).toString().toInt());
        dsoquery.exec();

        name[0] = name[1] = "";

        while (dsoquery.next() && k < 2) {
            tmpq.prepare("SELECT name FROM ctg WHERE rowid = :idctg");
            tmpq.bindValue(":idctg", dsoquery.value(1).toString().toInt());
            tmpq.exec();
            if (tmpq.next() && k < 2) {
                name[k] = tmpq.value(0).toString() + " " + dsoquery.value(0).toString();
                cat[k] = tmpq.value(0).toString();
                k++;
            }
        }
        longname = query.value(12).toString();

        dms r;
        r.setH( rah, ram, int(ras) );
        dms d( dd, dm, ds );

        if ( query.value(3).toString().toInt() == -1 ) { d.setD( -1.0*d.Degrees() ); }
        
        // create new deepskyobject
        DeepSkyObject *o = 0;
        if ( type==0 ) type = 1; //Make sure we use CATALOG_STAR, not STAR
        o = new DeepSkyObject( type, r, d, mag, name[0], name[1], longname, cat[0], a, b, pa, pgc, ugc );
        o->EquatorialToHorizontal( data->lst(), data->geo()->lat() );

        // Add the name(s) to the nameHash for fast lookup -jbb
        if (longname != "") {
            nameHash[ name[0].toLower() ] = o;
            if ( ! longname.isEmpty() ) nameHash[ longname.toLower() ] = o;
            if ( ! name[1].isEmpty() ) nameHash[ name[1].toLower() ] = o;
        }

        Trixel trixel = m_skyMesh->index( (SkyPoint*) o );

        //Assign object to general DeepSkyObjects list,
        //and a secondary list based on its catalog.
        m_DeepSkyList.append( o );
        appendIndex( o, &m_DeepSkyIndex, trixel );

        if ( o->isCatalogM()) {
            m_MessierList.append( o );
            appendIndex( o, &m_MessierIndex, trixel );
        }
        else if (o->isCatalogNGC() ) {
            m_NGCList.append( o );
            appendIndex( o, &m_NGCIndex, trixel );
        }
        else if ( o->isCatalogIC() ) {
            m_ICList.append( o );
            appendIndex( o, &m_ICIndex, trixel );
        }
        else {
            m_OtherList.append( o );
            appendIndex( o, &m_OtherIndex, trixel );
        }

        //Add name to the list of object names
        if ( ! name[0].isEmpty() )
            objectNames(type).append( name[0] );

        //Add long name to the list of object names
        if ( ! longname.isEmpty() && longname != name )
            objectNames(type).append( longname );

    }
}

void DeepSkyComponent::mergeSplitFiles() {
    //If user has downloaded the Steinicke NGC/IC catalog, then it is 
    //split into multiple files.  Concatenate these into a single file.
    QString firstFile = KStandardDirs::locateLocal("appdata", "ngcic01.dat");
    if ( ! QFile::exists( firstFile ) ) return;
    QDir localDir = QFileInfo( firstFile ).absoluteDir();
    QStringList catFiles = localDir.entryList( QStringList( "ngcic??.dat" ) );

    kDebug() << "Merging split NGC/IC files" << endl;

    QString buffer;
    foreach ( const QString &fname, catFiles ) {
        QFile f( localDir.absoluteFilePath(fname) );
        if ( f.open( QIODevice::ReadOnly ) ) {
            QTextStream stream( &f );
            buffer += stream.readAll();

            f.close();
        } else {
            kDebug() << QString("Error: Could not open %1 for reading").arg(fname) << endl;
        }
    }

    QFile fout( localDir.absoluteFilePath( "ngcic.dat" ) );
    if ( fout.open( QIODevice::WriteOnly ) ) {
        QTextStream oStream( &fout );
        oStream << buffer;
        fout.close();

        //Remove the split-files
        foreach ( const QString &fname, catFiles ) {
            QString fullname = localDir.absoluteFilePath(fname);
            //DEBUG
            kDebug() << "Removing " << fullname << " ..." << endl;
            QFile::remove( fullname );
        }
    }
}

void DeepSkyComponent::appendIndex( DeepSkyObject *o, DeepSkyIndex* dsIndex )
{
    MeshIterator region( m_skyMesh );
    while ( region.hasNext() ) {
        Trixel trixel = region.next();
        if ( ! dsIndex->contains( trixel ) ) {
            dsIndex->insert(trixel, new DeepSkyList() );
        }
        dsIndex->value( trixel )->append( o );
    }
}

void DeepSkyComponent::appendIndex( DeepSkyObject *o, DeepSkyIndex* dsIndex, Trixel trixel )
{
    if ( ! dsIndex->contains( trixel ) ) {
        dsIndex->insert(trixel, new DeepSkyList() );
    }
    dsIndex->value( trixel )->append( o );
}


void DeepSkyComponent::draw( QPainter& psky )
{
    if ( ! selected() ) return;

    bool drawFlag;

    drawFlag = Options::showMessier() &&
               ! ( Options::hideOnSlew() && Options::hideMessier() && SkyMap::IsSlewing() );

    drawDeepSkyCatalog( psky, drawFlag, &m_MessierIndex, "MessColor", Options::showMessierImages() );

    drawFlag = Options::showNGC() &&
               ! ( Options::hideOnSlew() && Options::hideNGC() && SkyMap::IsSlewing() );

    drawDeepSkyCatalog( psky, drawFlag,     &m_NGCIndex,     "NGCColor" );

    drawFlag = Options::showIC() &&
               ! ( Options::hideOnSlew() && Options::hideIC() && SkyMap::IsSlewing() );

    drawDeepSkyCatalog( psky, drawFlag,      &m_ICIndex,      "ICColor" );

    drawFlag = Options::showOther() &&
               ! ( Options::hideOnSlew() && Options::hideOther() && SkyMap::IsSlewing() );

    drawDeepSkyCatalog( psky, drawFlag,   &m_OtherIndex,   "NGCColor" );
}

void DeepSkyComponent::drawDeepSkyCatalog( QPainter& psky, bool drawObject,
                                           DeepSkyIndex* dsIndex, const QString& colorString, bool drawImage)
{
    if ( ! ( drawObject || drawImage ) ) return;

    SkyMap *map = SkyMap::Instance();
    KStarsData *data = KStarsData::Instance();

    UpdateID updateID = data->updateID();
    UpdateID updateNumID = data->updateNumID();

    psky.setPen( data->colorScheme()->colorNamed( colorString ) );
    psky.setBrush( Qt::NoBrush );
    QColor color        = data->colorScheme()->colorNamed( colorString );
    QColor colorExtra = data->colorScheme()->colorNamed( "HSTColor" );

    m_hideLabels =  ( map->isSlewing() && Options::hideOnSlew() ) ||
                    ! ( Options::showDeepSkyMagnitudes() || Options::showDeepSkyNames() );


    double maglim = Options::magLimitDrawDeepSky();

    //adjust maglimit for ZoomLevel
    double lgmin = log10(MINZOOM);
    double lgmax = log10(MAXZOOM);
    double lgz = log10(Options::zoomFactor());
    if ( lgz <= 0.75 * lgmax ) 
        maglim -= (Options::magLimitDrawDeepSky() - Options::magLimitDrawDeepSkyZoomOut() )*(0.75*lgmax - lgz)/(0.75*lgmax - lgmin);
    m_zoomMagLimit = maglim;

    double labelMagLim = Options::deepSkyLabelDensity();
    labelMagLim += ( Options::magLimitDrawDeepSky() - labelMagLim ) * ( lgz - lgmin) / (lgmax - lgmin );
    if ( labelMagLim > Options::magLimitDrawDeepSky() ) labelMagLim = Options::magLimitDrawDeepSky();


    //DrawID drawID = m_skyMesh->drawID();
    MeshIterator region( m_skyMesh, DRAW_BUF );

    while ( region.hasNext() ) {

        Trixel trixel = region.next();
        DeepSkyList* dsList = dsIndex->value( trixel );
        if ( dsList == 0 ) continue;
        for (int j = 0; j < dsList->size(); j++ ) {
            DeepSkyObject *obj = dsList->at( j );

            //if ( obj->drawID == drawID ) continue;  // only draw each line once
            //obj->drawID = drawID;

            if ( obj->updateID != updateID ) {
                obj->updateID = updateID;
                if ( obj->updateNumID != updateNumID) {
                    obj->updateCoords( data->updateNum() );
                }
                obj->EquatorialToHorizontal( data->lst(), data->geo()->lat() );
            }

            if ( ! map->checkVisibility( obj ) ) continue;

            float mag = obj->mag();
            float size = map->scale() * obj->a() * dms::PI * Options::zoomFactor() / 10800.0;

            //only draw objects if flags set, it's bigger than 1 pixel (unless
            //zoom > 2000.), and it's brighter than maglim (unless mag is
            //undefined (=99.9)
            if ( (size > 1.0 || Options::zoomFactor() > 2000.) &&
                 ( mag < (float)maglim || obj->isCatalogIC() ) ) {

                QPointF o = map->toScreen( obj );
                if ( ! map->onScreen( o ) ) continue;

                double PositionAngle = map->findPA( obj, o.x(), o.y() );

                //Draw Image
                bool imgdrawn = false;
                if ( drawImage && Options::zoomFactor() > 5.*MINZOOM ) {
                    imgdrawn = obj->drawImage( psky, o.x(), o.y(), PositionAngle, Options::zoomFactor() );
                }

                //Draw Symbol
                if ( drawObject ) {
                    //change color if extra images are available
                    // most objects don't have those, so we only change colors temporarily
                    // for the few exceptions. Changing color is expensive!!!
                    bool bColorChanged = false;
                    if ( obj->isCatalogM() && obj->ImageList().count() > 1 ) {
                        psky.setPen( colorExtra );
                        bColorChanged = true;
                    } else if ( (!obj->isCatalogM()) && obj->ImageList().count() ) {
                        psky.setPen( colorExtra );
                        bColorChanged = true;
                    }

                    obj->drawSymbol( psky, o.x(), o.y(), PositionAngle, Options::zoomFactor() );

                    if ( bColorChanged ) {
                        psky.setPen( color );
                    }
                }

                if ( !( drawObject || imgdrawn )  || ( m_hideLabels || mag > labelMagLim ) ) continue;
            
                addLabel( o, obj );
            }
            else { //Object failed checkVisible(); delete it's Image pointer, if it exists.
                if ( obj->image() ) {
                    obj->deleteImage();
                }
            }
        }
    }
}

void DeepSkyComponent::addLabel( const QPointF& p, DeepSkyObject *obj )
{
    int idx = int( obj->mag() * 10.0 );
    if ( idx < 0 ) idx = 0;
    if ( idx > MAX_LINENUMBER_MAG ) idx = MAX_LINENUMBER_MAG;
    m_labelList[ idx ]->append( SkyLabel( p, obj ) );
}

void DeepSkyComponent::drawLabels( QPainter& psky )
{
    if ( m_hideLabels ) return;

    psky.setPen( QColor( KStarsData::Instance()->colorScheme()->colorNamed( "DSNameColor" ) ) );

    int max = int( m_zoomMagLimit * 10.0 );
    if ( max < 0 ) max = 0;
    if ( max > MAX_LINENUMBER_MAG ) max = MAX_LINENUMBER_MAG;

    for ( int i = 0; i <= max; i++ ) {
        LabelList* list = m_labelList[ i ];
        for ( int j = 0; j < list->size(); j++ ) {
            list->at(j).obj->drawNameLabel( psky, list->at(j).o );
        }
        list->clear();
    }

}


SkyObject* DeepSkyComponent::findByName( const QString &name ) {

    return nameHash[ name.toLower() ];
}

void DeepSkyComponent::objectsInArea( QList<SkyObject*>& list, const SkyRegion& region ) 
{
    for( SkyRegion::const_iterator it = region.constBegin(); it != region.constEnd(); it++ )
    {
        Trixel trixel = it.key();
        if( m_DeepSkyIndex.contains( trixel ) )
        {
            DeepSkyList* dsoList = m_DeepSkyIndex.value(trixel);
            for( DeepSkyList::iterator dsit = dsoList->begin(); dsit != dsoList->end(); dsit++ )
                list.append( *dsit );
        }
    }
}


//we multiply each catalog's smallest angular distance by the
//following factors before selecting the final nearest object:
// IC catalog = 0.8
// NGC catalog = 0.5
// "other" catalog = 0.4
// Messier object = 0.25
SkyObject* DeepSkyComponent::objectNearest( SkyPoint *p, double &maxrad ) {

    if ( ! selected() ) return 0;

    SkyObject *oTry = 0;
    SkyObject *oBest = 0;
    double rTry = maxrad;
    double rBest = maxrad;
    double r;
    DeepSkyList* dsList;
    SkyObject* obj;

    MeshIterator region( m_skyMesh, OBJ_NEAREST_BUF );
    while ( region.hasNext() ) {
        dsList = m_ICIndex[ region.next() ];
        if ( ! dsList ) continue;
        for (int i=0; i < dsList->size(); ++i) {
            obj = dsList->at( i );
            r = obj->angularDistanceTo( p ).Degrees();
            if ( r < rTry ) {
                rTry = r;
                oTry = obj;
            }
        }
    }

    rTry *= 0.8;
    if ( rTry < rBest ) {
        rBest = rTry;
        oBest = oTry;
    }

    rTry = maxrad;
    region.reset();
    while ( region.hasNext() ) {
        dsList = m_NGCIndex[ region.next() ];
        if ( ! dsList ) continue;
        for (int i=0; i < dsList->size(); ++i) {
            obj = dsList->at( i );
            r = obj->angularDistanceTo( p ).Degrees();
            if ( r < rTry ) {
                rTry = r;
                oTry = obj;
            }
        }
    }

    rTry *= 0.6;
    if ( rTry < rBest ) {
        rBest = rTry;
        oBest = oTry;
    }

    rTry = maxrad;

    region.reset();
    while ( region.hasNext() ) {
        dsList = m_OtherIndex[ region.next() ];
        if ( ! dsList ) continue;
        for (int i=0; i < dsList->size(); ++i) {
            obj = dsList->at( i );
            r = obj->angularDistanceTo( p ).Degrees();
            if ( r < rTry ) {
                rTry = r;
                oTry = obj;
            }
        }
    }

    rTry *= 0.6;
    if ( rTry < rBest ) {
        rBest = rTry;
        oBest = oTry;
    }

    rTry = maxrad;

    region.reset();
    while ( region.hasNext() ) {
        dsList = m_MessierIndex[ region.next() ];
        if ( ! dsList ) continue;
        for (int i=0; i < dsList->size(); ++i) {
            obj = dsList->at( i );
            r = obj->angularDistanceTo( p ).Degrees();
            if ( r < rTry ) {
                rTry = r;
                oTry = obj;
            }
        }
    }

    // -jbb: this is the template of the non-indexed way
    //
    //foreach ( SkyObject *o, m_MessierList ) {
    //	r = o->angularDistanceTo( p ).Degrees();
    //	if ( r < rTry ) {
    //		rTry = r;
    //		oTry = o;
    //	}
    //}

    rTry *= 0.5;
    if ( rTry < rBest ) {
        rBest = rTry;
        oBest = oTry;
    }

    maxrad = rBest;
    return oBest;
}

void DeepSkyComponent::clearList(QList<DeepSkyObject*>& list) {
    while( !list.isEmpty() ) {
        SkyObject *o = list.takeFirst();
        removeFromNames( o );
        delete o;
    }
}

void DeepSkyComponent::clear() {
    clearList( m_MessierList );
    clearList( m_NGCList );
    clearList( m_ICList );
    clearList( m_OtherList );
}
