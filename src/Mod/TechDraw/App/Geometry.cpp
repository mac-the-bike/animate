/***************************************************************************
 *   Copyright (c) 2012 Luke Parry <l.parry@warwick.ac.uk>                 *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <boost/uuid/uuid_generators.hpp>
# include <boost/uuid/uuid_io.hpp>
# include <boost/random.hpp>
# include <Approx_Curve3d.hxx>
# include <BRep_Tool.hxx>
# include <BRepAdaptor_Curve.hxx>
# include <Mod/Part/App/FCBRepAlgoAPI_Section.h>
# include <BRepBuilderAPI_MakeEdge.hxx>
# include <BRepBuilderAPI_MakeFace.hxx>
# include <BRepBuilderAPI_MakeVertex.hxx>
# include <BRepBuilderAPI_MakeWire.hxx>
# include <BRepExtrema_DistShapeShape.hxx>
# include <BRepGProp.hxx>
# include <BRepLib.hxx>
# include <BRepLProp_CLProps.hxx>
# include <BRepTools.hxx>
#include <BRepLProp_CurveTool.hxx>
# include <GC_MakeArcOfCircle.hxx>
# include <GC_MakeEllipse.hxx>
#include <GC_MakeCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_Circle.hxx>

# include <gce_MakeCirc.hxx>
# include <GCPnts_AbscissaPoint.hxx>
# include <GProp_GProps.hxx>
# include <Geom_BSplineCurve.hxx>
# include <Geom_BezierCurve.hxx>
# include <GeomAPI_PointsToBSpline.hxx>
# include <GeomAPI_ProjectPointOnCurve.hxx>
# include <GeomConvert_BSplineCurveToBezierCurve.hxx>
# include <GeomLProp_CLProps.hxx>
# include <gp_Ax2.hxx>
# include <gp_Circ.hxx>
# include <gp_Dir.hxx>
# include <gp_Elips.hxx>
# include <gp_Pnt.hxx>
# include <gp_Vec.hxx>
# include <Poly_Polygon3D.hxx>
# include <Precision.hxx>
# include <Standard_Version.hxx>
# include <TColgp_Array1OfPnt.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
#if OCC_VERSION_HEX < 0x070600
# include <BRepAdaptor_HCurve.hxx>
#endif
#endif  // #ifndef _PreComp_

#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Base/Reader.h>
#include <Base/Tools.h>
#include <Base/Writer.h>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include <Mod/Part/App/FaceMakerCheese.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/TopoShape.h>

#include "DrawViewPart.h"
#include "Geometry.h"
#include "ShapeUtils.h"
#include "DrawUtil.h"


using namespace TechDraw;
using namespace Part;
using namespace std;
using DU = DrawUtil;

#if OCC_VERSION_HEX >= 0x070600
using BRepAdaptor_HCurve = BRepAdaptor_Curve;
#endif

#define GEOMETRYEDGE 0
#define COSMETICEDGE 1
#define CENTERLINE   2

// Collection of Geometric Features
Wire::Wire()
{
}

Wire::Wire(const TopoDS_Wire &w)
{
    TopExp_Explorer edges(w, TopAbs_EDGE);
    for (; edges.More(); edges.Next()) {
        const auto edge( TopoDS::Edge(edges.Current()) );
        BaseGeomPtr bg = BaseGeom::baseFactory(edge);
        if (bg) {
            geoms.push_back(bg);
        }
    }
}

Wire::~Wire()
{
    //shared_ptr to geoms should free memory when ref count goes to zero
    geoms.clear();
}

TopoDS_Wire Wire::toOccWire() const
{
    BRepBuilderAPI_MakeWire mkWire;
    for (auto& g: geoms) {
        TopoDS_Edge e = g->getOCCEdge();
        mkWire.Add(e);
    }
    if (mkWire.IsDone())  {
        return mkWire.Wire();
    }
//    BRepTools::Write(result, "toOccWire.brep");
    return TopoDS_Wire();
}

void Wire::dump(std::string s)
{
    BRepTools::Write(toOccWire(), s.c_str());            //debug
}

// note that the face returned is inverted in Y
TopoDS_Face Face::toOccFace() const
{
    if (wires.empty()) {
        return {};
    }

    TopoDS_Face result;
    BRepBuilderAPI_MakeFace mkFace(wires.front()->toOccWire(), true);
    int limit = wires.size();
    int iwire = 1;
    for ( ; iwire < limit; iwire++) {
//        w->dump("wireInToFace.brep");
        TopoDS_Wire wOCC = wires.at(iwire)->toOccWire();
        if(!wOCC.IsNull())  {
            mkFace.Add(wOCC);
        }
    }
    if (mkFace.IsDone())  {
        return mkFace.Face();
    }
    return TopoDS_Face();
}

//**** Face
Base::Vector3d Face::getCenter() const {
    GProp_GProps faceProps;
    BRepGProp::SurfaceProperties(toOccFace(), faceProps);

    return DrawUtil::toVector3d(faceProps.CentreOfMass());
}

double Face::getArea() const {
    GProp_GProps faceProps;
    BRepGProp::SurfaceProperties(toOccFace(), faceProps);

    return faceProps.Mass();
}

Face::~Face()
{
    for(auto it : wires) {
        delete it;
    }
    wires.clear();
}

BaseGeom::BaseGeom() :
    geomType(NOTDEF),
    extractType(Plain),             //obs
    classOfEdge(ecNONE),
    hlrVisible(true),
    reversed(false),
    ref3D(-1),                      //obs?
    cosmetic(false),
    m_source(0),
    m_sourceIndex(-1)
{
    occEdge = TopoDS_Edge();
    cosmeticTag = std::string();
    tag = boost::uuids::nil_uuid();
}

BaseGeomPtr BaseGeom::copy()
{
    BaseGeomPtr result;
    if (!occEdge.IsNull()) {
        result = baseFactory(occEdge);
        if (!result) {
            result = std::make_shared<BaseGeom>();
        }
    }

    result->extractType = extractType;
    result->classOfEdge = classOfEdge;
    result->setHlrVisible( hlrVisible);
    result->reversed = reversed;
    result->ref3D = ref3D;
    result->cosmetic = cosmetic;
    result->source(m_source);
    result->sourceIndex(m_sourceIndex);
    result->cosmeticTag = cosmeticTag;

    return result;
}

std::string BaseGeom::toString() const
{
    std::stringstream ss;
    ss << geomType << ", " <<
          extractType << ", " <<
          classOfEdge << ", " <<
          hlrVisible << ", " <<
          reversed << ", " <<
          ref3D << ", " <<
          cosmetic << ", " <<
          m_source << ", " <<
          m_sourceIndex;
    return ss.str();
}

boost::uuids::uuid BaseGeom::getTag() const
{
    return tag;
}

std::string BaseGeom::getTagAsString() const
{
    return boost::uuids::to_string(getTag());
}

void BaseGeom::Save(Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<GeomType value=\"" << geomType << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<ExtractType value=\"" << extractType << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<EdgeClass value=\"" << classOfEdge << "\"/>" << endl;
    const char v = hlrVisible?'1':'0';
    writer.Stream() << writer.ind() << "<HLRVisible value=\"" <<  v << "\"/>" << endl;
    const char r = reversed?'1':'0';
    writer.Stream() << writer.ind() << "<Reversed value=\"" << r << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<Ref3D value=\"" << ref3D << "\"/>" << endl;
    const char c = cosmetic?'1':'0';
    writer.Stream() << writer.ind() << "<Cosmetic value=\"" << c << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<Source value=\"" << m_source << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<SourceIndex value=\"" << m_sourceIndex << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<CosmeticTag value=\"" <<  cosmeticTag << "\"/>" << endl;
//    writer.Stream() << writer.ind() << "<Tag value=\"" <<  getTagAsString() << "\"/>" << endl;
}

void BaseGeom::Restore(Base::XMLReader &reader)
{
    reader.readElement("GeomType");
    geomType = static_cast<TechDraw::GeomType>(reader.getAttributeAsInteger("value"));
    reader.readElement("ExtractType");
    extractType = static_cast<TechDraw::ExtractionType>(reader.getAttributeAsInteger("value"));
    reader.readElement("EdgeClass");
    classOfEdge = static_cast<TechDraw::edgeClass>(reader.getAttributeAsInteger("value"));
    reader.readElement("HLRVisible");
    hlrVisible = reader.getAttributeAsInteger("value") != 0;
    reader.readElement("Reversed");
    reversed = reader.getAttributeAsInteger("value") != 0;
    reader.readElement("Ref3D");
    ref3D = reader.getAttributeAsInteger("value");
    reader.readElement("Cosmetic");
    cosmetic = reader.getAttributeAsInteger("value") != 0;
    reader.readElement("Source");
    m_source = reader.getAttributeAsInteger("value");
    reader.readElement("SourceIndex");
    m_sourceIndex = reader.getAttributeAsInteger("value");
    reader.readElement("CosmeticTag");
    cosmeticTag = reader.getAttribute("value");
}

std::vector<Base::Vector3d> BaseGeom::findEndPoints()
{
    std::vector<Base::Vector3d> result;

    if (!occEdge.IsNull()) {
        gp_Pnt p = BRep_Tool::Pnt(TopExp::FirstVertex(occEdge));
        result.emplace_back(p.X(), p.Y(), p.Z());
        p = BRep_Tool::Pnt(TopExp::LastVertex(occEdge));
        result.emplace_back(p.X(), p.Y(), p.Z());
    } else {
        //TODO: this should throw something
        Base::Console().Message("Geometry::findEndPoints - OCC edge not found\n");
        throw Base::RuntimeError("no OCC edge in Geometry::findEndPoints");
    }
    return result;
}


Base::Vector3d BaseGeom::getStartPoint()
{
    std::vector<Base::Vector3d> verts = findEndPoints();
    if (!verts.empty()) {
        return verts[0];
    } else {
        //TODO: this should throw something
        Base::Console().Message("Geometry::getStartPoint - start point not found!\n");
        Base::Vector3d badResult(0.0, 0.0, 0.0);
        return badResult;
    }
}


Base::Vector3d BaseGeom::getEndPoint()
{
    std::vector<Base::Vector3d> verts = findEndPoints();

    if (verts.size() != 2) {
        //TODO: this should throw something
        Base::Console().Message("Geometry::getEndPoint - end point not found!\n");
        Base::Vector3d badResult(0.0, 0.0, 0.0);
        return badResult;
    }
    return verts[1];
}

Base::Vector3d BaseGeom::getMidPoint()
{
    // Midpoint calculation - additional details here: https://forum.freecad.org/viewtopic.php?f=35&t=59582

    BRepAdaptor_Curve curve(occEdge);

    // As default, set the midpoint curve parameter value by simply averaging start point and end point values
    double midParam = (curve.FirstParameter() + curve.LastParameter())/2.0;

    // GCPnts_AbscissaPoint allows us to compute the parameter value depending on the distance along a curve.
    // In this case we want the curve parameter value for the half of the whole curve length,
    // thus GCPnts_AbscissaPoint::Length(curve)/2 is the distance to go from the start point.
    GCPnts_AbscissaPoint abscissa(Precision::Confusion(), curve, GCPnts_AbscissaPoint::Length(curve)/2.0,
                                  curve.FirstParameter());
    if (abscissa.IsDone()) {
        // The computation was successful - otherwise keep the average, it is better than nothing
        midParam = abscissa.Parameter();
    }

    // Now compute coordinates of the point on curve for curve parameter value equal to midParam
    BRepLProp_CLProps props(curve, midParam, 0, Precision::Confusion());
    const gp_Pnt &point = props.Value();

    return Base::Vector3d(point.X(), point.Y(), point.Z());
}

std::vector<Base::Vector3d> BaseGeom::getQuads()
{
    std::vector<Base::Vector3d> result;
    BRepAdaptor_Curve adapt(occEdge);
    double u = adapt.FirstParameter();
    double v = adapt.LastParameter();
    double range = v - u;
    double q1 = u + (range / 4.0);
    double q2 = u + (range / 2.0);
    double q3 = u + (3.0 * range / 4.0);
    BRepLProp_CLProps prop(adapt, q1, 0, Precision::Confusion());
    const gp_Pnt& p1 = prop.Value();
    result.emplace_back(p1.X(), p1.Y(), 0.0);
    prop.SetParameter(q2);
    const gp_Pnt& p2 = prop.Value();
    result.emplace_back(p2.X(), p2.Y(), 0.0);
    prop.SetParameter(q3);
    const gp_Pnt& p3 = prop.Value();
    result.emplace_back(p3.X(), p3.Y(), 0.0);
    return result;
}

double BaseGeom::minDist(Base::Vector3d p)
{
    gp_Pnt pnt(p.x, p.y,0.0);
    TopoDS_Vertex v = BRepBuilderAPI_MakeVertex(pnt);
    return TechDraw::DrawUtil::simpleMinDist(occEdge, v);
}

//!find point on me nearest to p
Base::Vector3d BaseGeom::nearPoint(const BaseGeomPtr p)
{
    TopoDS_Edge pEdge = p->getOCCEdge();
    BRepExtrema_DistShapeShape extss(occEdge, pEdge);
    if (!extss.IsDone() || extss.NbSolution() == 0) {
        return Base::Vector3d(0.0, 0.0, 0.0);
    }
    gp_Pnt p1 = extss.PointOnShape1(1);
    return Base::Vector3d(p1.X(), p1.Y(), 0.0);
}

Base::Vector3d BaseGeom::nearPoint(Base::Vector3d p)
{
    gp_Pnt pnt(p.x, p.y,0.0);
    TopoDS_Vertex v = BRepBuilderAPI_MakeVertex(pnt);
    BRepExtrema_DistShapeShape extss(occEdge, v);
    if (!extss.IsDone() || extss.NbSolution() == 0) {
        return Base::Vector3d(0.0, 0.0, 0.0);
    }
    gp_Pnt p1 = extss.PointOnShape1(1);
    return Base::Vector3d(p1.X(), p1.Y(), 0.0);
}

std::string BaseGeom::dump()
{
    Base::Vector3d start = getStartPoint();
    Base::Vector3d end   = getEndPoint();
    std::stringstream ss;
    ss << "BaseGeom: s:(" << start.x << ", " << start.y << ") e:(" << end.x << ", " << end.y << ") ";
    ss << "type: " << geomType << " class: " << classOfEdge << " viz: " << hlrVisible << " rev: " << reversed;
    ss << "cosmetic: " << cosmetic << " source: " << source() << " iSource: " << sourceIndex();
    return ss.str();
}

bool BaseGeom::closed()
{
    Base::Vector3d start(getStartPoint().x,
                         getStartPoint().y,
                         0.0);
    Base::Vector3d end(getEndPoint().x,
                       getEndPoint().y,
                       0.0);
    if (start.IsEqual(end, 0.00001)) {
        return true;
    }
    return false;
}

// return a BaseGeom similar to this, but inverted with respect to Y axis
BaseGeomPtr BaseGeom::inverted()
{
//    Base::Console().Message("BG::inverted()\n");
    TopoDS_Shape invertedShape = ShapeUtils::invertGeometry(occEdge);
    TopoDS_Edge invertedEdge = TopoDS::Edge(invertedShape);
    return baseFactory(invertedEdge);
}

//keep this in sync with enum GeomType
std::string BaseGeom::geomTypeName()
{
    std::vector<std::string> typeNames {
        "NotDefined",
        "Circle",
        "ArcOfCircle",
        "Ellipse",
        "ArcOfEllipse",
        "Bezier",
        "BSpline",
        "Line",         //why was this ever called "Generic"?
        "Unknown" } ;
    if (geomType >= typeNames.size()) {
        return "Unknown";
    }
    return typeNames.at(geomType);
}

//! Convert 1 OCC edge into 1 BaseGeom (static factory method)
// this should not return nullptr as things will break later on.
// regular geometry is stored scaled, but cosmetic geometry is stored in 1:1 scale, so the crazy edge
// check is not appropriate.
BaseGeomPtr BaseGeom::baseFactory(TopoDS_Edge edge, bool isCosmetic)
{
    if (edge.IsNull()) {
        Base::Console().Message("BG::baseFactory - input edge is NULL \n");
    }
    //weed out rubbish edges before making geometry
    if (!isCosmetic && !validateEdge(edge)) {
        return nullptr;
    }

    BaseGeomPtr result = std::make_shared<Generic> (edge);

    BRepAdaptor_Curve adapt(edge);
    switch(adapt.GetType()) {
      case GeomAbs_Circle: {
        double f = adapt.FirstParameter();
        double l = adapt.LastParameter();
        gp_Pnt s = adapt.Value(f);
        gp_Pnt e = adapt.Value(l);

        //don't understand this test.
        //if first to last is > 1 radian? are circles parameterize by rotation angle?
        //if start and end points are close?
        if (fabs(l-f) > 1.0 && s.SquareDistance(e) < 0.001) {
              result = std::make_shared<Circle>(edge);
        } else {
              result = std::make_shared<AOC>(edge);
        }
      } break;
      case GeomAbs_Ellipse: {
        double f = adapt.FirstParameter();
        double l = adapt.LastParameter();
        gp_Pnt s = adapt.Value(f);
        gp_Pnt e = adapt.Value(l);
        if (fabs(l-f) > 1.0 && s.SquareDistance(e) < 0.001) {
              result = std::make_shared<Ellipse>(edge);
        } else {
              result = std::make_shared<AOE>(edge);
        }
      } break;
      case GeomAbs_BezierCurve: {
          Handle(Geom_BezierCurve) bez = adapt.Bezier();
          //if (bez->Degree() < 4) {
          result = std::make_shared<BezierSegment>(edge);
          if (edge.Orientation() == TopAbs_REVERSED) {
              result->reversed = true;
          }

          //    OCC is quite happy with Degree > 3 but QtGui handles only 2, 3
      } break;
      case GeomAbs_BSplineCurve: {
        TopoDS_Edge circEdge;

        bool isArc = false;
        try {
            BSplinePtr bspline = std::make_shared<BSpline>(edge);
            if (bspline->isLine()) {
                result = std::make_shared<Generic>(edge);
            } else if (bspline->isCircle())  {
                circEdge = bspline->asCircle(isArc);
                if (circEdge.IsNull()) {
                    result = bspline;
                } else {
                    if (isArc) {
                        result = std::make_shared<AOC>(circEdge);
                    } else {
                        result = std::make_shared<Circle>(circEdge);
                    }
                 }
            } else {
//              Base::Console().Message("Geom::baseFactory - circEdge is Null\n");
                result = bspline;
            }
            break;
        }
        catch (const Standard_Failure& e) {
            Base::Console().Log("Geom::baseFactory - OCC error - %s - while making spline\n",
                              e.GetMessageString());
            break;
        }
        catch (...) {
            Base::Console().Log("Geom::baseFactory - unknown error occurred while making spline\n");
            break;
        } break;
      } // end bspline case
      default: {
        result = std::make_unique<Generic>(edge);
      }  break;
    }

    return result;
}

bool BaseGeom::validateEdge(TopoDS_Edge edge)
{
    return !DrawUtil::isCrazy(edge);
}

TopoDS_Edge BaseGeom::completeEdge(const TopoDS_Edge &edge) {
    // Extend given edge so we can get intersections even outside its boundaries
    try {
        BRepAdaptor_Curve curve(edge);
        switch (curve.GetType()) {
            case GeomAbs_Line:
                // Edge longer than 10m is considered "crazy", thus limit intersection(s) to this perimeter
                return BRepBuilderAPI_MakeEdge(curve.Line(), -10000.0, +10000.0);
            case GeomAbs_Circle:
                // If an arc of circle was provided, return full circle
                return BRepBuilderAPI_MakeEdge(curve.Circle());
            case GeomAbs_Ellipse:
                // If an arc of ellipse was provided, return full ellipse
                return BRepBuilderAPI_MakeEdge(curve.Ellipse());
            default:
                // Currently we are not extrapolating B-splines, though it is technically possible
                return BRepBuilderAPI_MakeEdge(curve.Curve().Curve());
        }
    }
    catch (Standard_Failure &e) {
        Base::Console().Error("BaseGeom::completeEdge OCC error: %s\n", e.GetMessageString());
    }

    return TopoDS_Edge();
}

std::vector<Base::Vector3d> BaseGeom::intersection(TechDraw::BaseGeomPtr geom2)
{
    // find intersection vertex(es) between two edges
    // call: interPoints = line1.intersection(line2);
    std::vector<Base::Vector3d> interPoints;

    TopoDS_Edge edge1 = completeEdge(this->getOCCEdge());
    if (edge1.IsNull()) {
        return interPoints;
    }

    TopoDS_Edge edge2 = completeEdge(geom2->getOCCEdge());
    if (edge2.IsNull()) {
        return interPoints;
    }

    FCBRepAlgoAPI_Section sectionOp(edge1, edge2);
    sectionOp.SetFuzzyValue(FUZZYADJUST*EWTOLERANCE);
    sectionOp.SetNonDestructive(true);

    sectionOp.Build();
    if (!sectionOp.HasErrors()) {
        TopoDS_Shape sectionShape = sectionOp.Shape();
        if (!sectionShape.IsNull()) {
            TopExp_Explorer explorer(sectionShape, TopAbs_VERTEX);
            while (explorer.More()) {
                Base::Vector3d pt(DrawUtil::toVector3d(BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current()))));
                interPoints.push_back(pt);
                explorer.Next();
            }
        }
    }

    return interPoints;
}

TopoShape BaseGeom::asTopoShape(double scale)
{
//    Base::Console().Message("BG::asTopoShape(%.3f) - dump: %s\n", scale, dump().c_str());
    TopoDS_Shape unscaledShape = ShapeUtils::scaleShape(getOCCEdge(), 1.0 / scale);
    TopoDS_Edge unscaledEdge = TopoDS::Edge(unscaledShape);
    return unscaledEdge;
}

Ellipse::Ellipse(const TopoDS_Edge &e)
{
    geomType = ELLIPSE;
    BRepAdaptor_Curve c(e);
    occEdge = e;
    gp_Elips ellp = c.Ellipse();
    const gp_Pnt &p = ellp.Location();

    center = Base::Vector3d(p.X(), p.Y(), 0.0);

    major = ellp.MajorRadius();
    minor = ellp.MinorRadius();

    gp_Dir xaxis = ellp.XAxis().Direction();
    angle = xaxis.AngleWithRef(gp_Dir(1, 0, 0), gp_Dir(0, 0, -1));
}

Ellipse::Ellipse(Base::Vector3d c, double mnr, double mjr)
{
    geomType = ELLIPSE;
    center = c;
    major = mjr;
    minor = mnr;
    angle = 0;

    GC_MakeEllipse me(gp_Ax2(gp_Pnt(c.x, c.y, c.z), gp_Dir(0.0, 0.0, 1.0)),
                      major, minor);
    if (!me.IsDone()) {
        Base::Console().Message("G:Ellipse - failed to make Ellipse\n");
    }
    const Handle(Geom_Ellipse) gEllipse = me.Value();
    BRepBuilderAPI_MakeEdge mkEdge(gEllipse, 0.0, 2 * M_PI);
    if (mkEdge.IsDone()) {
        occEdge = mkEdge.Edge();
    }
}

AOE::AOE(const TopoDS_Edge &e) : Ellipse(e)
{
    geomType = ARCOFELLIPSE;

    BRepAdaptor_Curve c(e);
    double f = c.FirstParameter();
    double l = c.LastParameter();
    gp_Pnt s = c.Value(f);
    gp_Pnt m = c.Value((l+f)/2.0);
    gp_Pnt ePt = c.Value(l);

    double a;
    try {
        gp_Vec v1(m, s);
        gp_Vec v2(m, ePt);
        gp_Vec v3(0, 0, 1);
        a = v3.DotCross(v1, v2);
    }
    catch (const Standard_Failure& e) {
        Base::Console().Error("Geom::AOE::AOE - OCC error - %s - while making AOE in ctor\n",
                              e.GetMessageString());
    }

    startAngle = fmod(f, 2.0*M_PI);
    endAngle = fmod(l, 2.0*M_PI);
    cw = (a < 0) ? true: false;
    largeArc = (l-f > M_PI) ? true : false;

    startPnt = Base::Vector3d(s.X(), s.Y(), s.Z());
    endPnt = Base::Vector3d(ePt.X(), ePt.Y(), ePt.Z());
    midPnt = Base::Vector3d(m.X(), m.Y(), m.Z());
    if (e.Orientation() == TopAbs_REVERSED) {
        reversed = true;
    }
}


Circle::Circle()
{
    geomType = CIRCLE;
    radius = 0.0;
    center = Base::Vector3d(0.0, 0.0, 0.0);
}

Circle::Circle(Base::Vector3d c, double r)
{
    geomType = CIRCLE;
    radius = r;
    center = c;
    gp_Pnt loc(c.x, c.y, c.z);
    gp_Dir dir(0, 0, 1);
    gp_Ax1 axis(loc, dir);
    gp_Circ circle;
    circle.SetAxis(axis);
    circle.SetRadius(r);
    double angle1 = 0.0;
    double angle2 = 360.0;

    Handle(Geom_Circle) hCircle = new Geom_Circle (circle);
    BRepBuilderAPI_MakeEdge aMakeEdge(hCircle, angle1*(M_PI/180), angle2*(M_PI/180));
    TopoDS_Edge edge = aMakeEdge.Edge();
    occEdge = edge;
}


Circle::Circle(const TopoDS_Edge &e)
{
    geomType = CIRCLE;             //center, radius
    BRepAdaptor_Curve c(e);
    occEdge = e;

    gp_Circ circ = c.Circle();
    const gp_Pnt& p = circ.Location();

    radius = circ.Radius();
    center = Base::Vector3d(p.X(), p.Y(), p.Z());
}
std::string Circle::toString() const
{
    std::string baseCSV = BaseGeom::toString();
    std::stringstream ss;
    ss << center.x << ", " <<
          center.y << ", " <<
          center.z << ", " <<
          radius;
    return baseCSV + ", $$$, " + ss.str();
}

void Circle::Save(Base::Writer &writer) const
{
    BaseGeom::Save(writer);
    writer.Stream() << writer.ind() << "<Center "
                << "X=\"" <<  center.x <<
                "\" Y=\"" <<  center.y <<
                "\" Z=\"" <<  center.z <<
                 "\"/>" << endl;

    writer.Stream() << writer.ind() << "<Radius value=\"" << radius << "\"/>" << endl;
}

void Circle::Restore(Base::XMLReader &reader)
{
    BaseGeom::Restore(reader);
    // read my Element
    reader.readElement("Center");
    // get the value of my Attribute
    center.x = reader.getAttributeAsFloat("X");
    center.y = reader.getAttributeAsFloat("Y");
    center.z = reader.getAttributeAsFloat("Z");

    reader.readElement("Radius");
    radius = reader.getAttributeAsFloat("value");
}

AOC::AOC(const TopoDS_Edge &e) : Circle(e)
{
    geomType = ARCOFCIRCLE;
    BRepAdaptor_Curve c(e);

    double f = c.FirstParameter();
    double l = c.LastParameter();
    gp_Pnt s = c.Value(f);
    gp_Pnt m = c.Value((l+f)/2.0);
    gp_Pnt ePt = c.Value(l);           //if start == end, it isn't an arc!
    gp_Vec v1(m, s);        //vector mid to start
    gp_Vec v2(m, ePt);      //vector mid to end
    gp_Vec v3(0, 0, 1);      //stdZ

    // this is the wrong determination of cw/ccw.  needs to be determined by edge.
    double a = v3.DotCross(v1, v2);    //error if v1 = v2?

    startAngle = fmod(f, 2.0*M_PI);
    endAngle = fmod(l, 2.0*M_PI);


    cw = (a < 0) ? true: false;
    largeArc = (fabs(l-f) > M_PI) ? true : false;

    startPnt = DU::toVector3d(s);
    endPnt = DU::toVector3d(ePt);
    midPnt = DU::toVector3d(m);
    if (e.Orientation() == TopAbs_REVERSED) {
        reversed = true;
    }
}

AOC::AOC(Base::Vector3d c, double r, double sAng, double eAng) : Circle()
{
    geomType = ARCOFCIRCLE;

    radius = r;
    center = c;
    gp_Pnt loc(c.x, c.y, c.z);
    gp_Dir dir(0, 0, 1);
    gp_Ax1 axis(loc, dir);
    gp_Circ circle;
    circle.SetAxis(axis);
    circle.SetRadius(r);

    Handle(Geom_Circle) hCircle = new Geom_Circle (circle);
    BRepBuilderAPI_MakeEdge aMakeEdge(hCircle, sAng*(M_PI/180), eAng*(M_PI/180));
    TopoDS_Edge edge = aMakeEdge.Edge();
    occEdge = edge;

    BRepAdaptor_Curve adp(edge);

    double f = adp.FirstParameter();
    double l = adp.LastParameter();
    gp_Pnt s = adp.Value(f);
    gp_Pnt m = adp.Value((l+f)/2.0);
    gp_Pnt ePt = adp.Value(l);           //if start == end, it isn't an arc!
    gp_Vec v1(m, s);        //vector mid to start
    gp_Vec v2(m, ePt);      //vector mid to end
    gp_Vec v3(0, 0, 1);      //stdZ

    // this is a bit of an arcane method of determining if v2 is clockwise from v1 or counter clockwise from v1.
    // The v1 x v2 points up if v2 is ccw from v1 and points down if v2 is cw from v1.  Taking (v1 x v2) * stdZ
    // gives 1 for parallel with stdZ (v2 is ccw from v1) or -1 for antiparallel with stdZ (v2 is clockwise from v1).
    // this cw flag is a problem.  we should just declare that arcs are always ccw and flip the start and end angles.
    double a = v3.DotCross(v1, v2);    //error if v1 = v2?

    startAngle = fmod(f, 2.0*M_PI);
    endAngle = fmod(l, 2.0*M_PI);
    cw = (a < 0) ? true: false;
    largeArc = (fabs(l-f) > M_PI) ? true : false;

    startPnt = DU::toVector3d(s);
    endPnt = DU::toVector3d(ePt);
    midPnt = DU::toVector3d(m);
    if (edge.Orientation() == TopAbs_REVERSED) {
        reversed = true;
    }
}


AOC::AOC() : Circle()
{
    geomType = ARCOFCIRCLE;

    startPnt = Base::Vector3d(0.0, 0.0, 0.0);
    endPnt = Base::Vector3d(0.0, 0.0, 0.0);
    midPnt = Base::Vector3d(0.0, 0.0, 0.0);
    startAngle = 0.0;
    endAngle = 2.0 * M_PI;
    cw = false;
    largeArc = false;

}

bool AOC::isOnArc(Base::Vector3d p)
{
    gp_Pnt pnt(p.x, p.y,p.z);
    TopoDS_Vertex v = BRepBuilderAPI_MakeVertex(pnt);
    BRepExtrema_DistShapeShape extss(occEdge, v);
    if (!extss.IsDone() || extss.NbSolution() == 0) {
        return false;
    }
    double minDist = extss.Value();
    if (minDist < Precision::Confusion()) {
        return true;
    }
    return false;
}

BaseGeomPtr AOC::copy()
{
    auto base = BaseGeom::copy();
    TechDraw::CirclePtr circle =  std::static_pointer_cast<TechDraw::Circle>(base);
    TechDraw::AOCPtr aoc = std::static_pointer_cast<TechDraw::AOC>(circle);
    if (aoc) {
        aoc->clockwiseAngle(clockwiseAngle());
        aoc->startPnt = startPnt;
        aoc->startAngle = startAngle;
        aoc->endPnt = endPnt;
        aoc->endAngle = endAngle;
        aoc->largeArc = largeArc;
    }
    return base;
}

double AOC::distToArc(Base::Vector3d p)
{
    return minDist(p);
}


bool AOC::intersectsArc(Base::Vector3d p1, Base::Vector3d p2)
{
    gp_Pnt pnt1(p1.x, p1.y,p1.z);
    TopoDS_Vertex v1 = BRepBuilderAPI_MakeVertex(pnt1);
    gp_Pnt pnt2(p2.x, p2.y, p2.z);
    TopoDS_Vertex v2 = BRepBuilderAPI_MakeVertex(pnt2);
    BRepBuilderAPI_MakeEdge mkEdge(v1, v2);
    TopoDS_Edge line = mkEdge.Edge();
    BRepExtrema_DistShapeShape extss(occEdge, line);
    if (!extss.IsDone() || extss.NbSolution() == 0) {
        return false;
    }
    double minDist = extss.Value();
    if (minDist < Precision::Confusion()) {
        return true;
    }
    return false;
}

std::string AOC::toString() const
{
    std::string circleCSV = Circle::toString();
    std::stringstream ss;

    ss << startPnt.x << ", " <<
          startPnt.y << ", " <<
          startPnt.z << ", " <<
          endPnt.x << ", " <<
          endPnt.y << ", " <<
          endPnt.z << ", " <<
          midPnt.x << ", " <<
          midPnt.y << ", " <<
          midPnt.z << ", " <<
          startAngle << ", " <<
          endAngle << ", " <<
          cw << ", " <<
          largeArc;

    return circleCSV + ", $$$," + ss.str();
}

void AOC::Save(Base::Writer &writer) const
{
    Circle::Save(writer);
    writer.Stream() << writer.ind() << "<Start "
                << "X=\"" <<  startPnt.x <<
                "\" Y=\"" <<  startPnt.y <<
                "\" Z=\"" <<  startPnt.z <<
                 "\"/>" << endl;
    writer.Stream() << writer.ind() << "<End "
                << "X=\"" <<  endPnt.x <<
                "\" Y=\"" <<  endPnt.y <<
                "\" Z=\"" <<  endPnt.z <<
                 "\"/>" << endl;
    writer.Stream() << writer.ind() << "<Middle "
                << "X=\"" <<  midPnt.x <<
                "\" Y=\"" <<  midPnt.y <<
                "\" Z=\"" <<  midPnt.z <<
                 "\"/>" << endl;
    writer.Stream() << writer.ind() << "<StartAngle value=\"" << startAngle << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<EndAngle value=\"" << endAngle << "\"/>" << endl;
    const char c = cw?'1':'0';
    writer.Stream() << writer.ind() << "<Clockwise value=\"" <<  c << "\"/>" << endl;
    const char la = largeArc?'1':'0';
    writer.Stream() << writer.ind() << "<Large value=\"" <<  la << "\"/>" << endl;
}

void AOC::Restore(Base::XMLReader &reader)
{
    Circle::Restore(reader);
    reader.readElement("Start");
    startPnt.x = reader.getAttributeAsFloat("X");
    startPnt.y = reader.getAttributeAsFloat("Y");
    startPnt.z = reader.getAttributeAsFloat("Z");
    reader.readElement("End");
    endPnt.x = reader.getAttributeAsFloat("X");
    endPnt.y = reader.getAttributeAsFloat("Y");
    endPnt.z = reader.getAttributeAsFloat("Z");
    reader.readElement("Middle");
    midPnt.x = reader.getAttributeAsFloat("X");
    midPnt.y = reader.getAttributeAsFloat("Y");
    midPnt.z = reader.getAttributeAsFloat("Z");

    reader.readElement("StartAngle");
    startAngle = reader.getAttributeAsFloat("value");
    reader.readElement("EndAngle");
    endAngle = reader.getAttributeAsFloat("value");
    reader.readElement("Clockwise");
    cw = (int)reader.getAttributeAsInteger("value")==0?false:true;
    reader.readElement("Large");
    largeArc = (int)reader.getAttributeAsInteger("value")==0?false:true;
}

//! Generic is a multiline
Generic::Generic(const TopoDS_Edge &e)
{
    geomType = GENERIC;
    occEdge = e;
    BRepLib::BuildCurve3d(occEdge);

    TopLoc_Location location;
    Handle(Poly_Polygon3D) polygon = BRep_Tool::Polygon3D(occEdge, location);

    if (!polygon.IsNull()) {
        const TColgp_Array1OfPnt &nodes = polygon->Nodes();
        for (int i = nodes.Lower(); i <= nodes.Upper(); i++){
            points.emplace_back(nodes(i).X(), nodes(i).Y(), nodes(i).Z());
        }
    } else {
        //no polygon representation? approximate with line
        gp_Pnt p = BRep_Tool::Pnt(TopExp::FirstVertex(occEdge));
        points.emplace_back(p.X(), p.Y(), p.Z());
        p = BRep_Tool::Pnt(TopExp::LastVertex(occEdge));
        points.emplace_back(p.X(), p.Y(), p.Z());
    }
    if (e.Orientation() == TopAbs_REVERSED) {
        reversed = true;
    }
}


Generic::Generic()
{
    geomType = GENERIC;
}

std::string Generic::toString() const
{
    std::string baseCSV = BaseGeom::toString();
    std::stringstream ss;
    ss << points.size() << ", ";
    for (auto& p: points) {
        ss << p.x << ", " <<
              p.y << ", " <<
              p.z << ", ";
    }
    std::string genericCSV = ss.str();
    genericCSV.pop_back();
    return baseCSV + ", $$$, " + genericCSV;
}


void Generic::Save(Base::Writer &writer) const
{
    BaseGeom::Save(writer);
    writer.Stream() << writer.ind()
                        << "<Points PointsCount =\"" << points.size() << "\">" << endl;
    writer.incInd();
    for (auto& p: points) {
        writer.Stream() << writer.ind() << "<Point "
                    << "X=\"" <<  p.x <<
                    "\" Y=\"" <<  p.y <<
                    "\" Z=\"" <<  p.z <<
                     "\"/>" << endl;
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Points>" << endl ;

}

void Generic::Restore(Base::XMLReader &reader)
{
    BaseGeom::Restore(reader);
    reader.readElement("Points");
    int stop = reader.getAttributeAsInteger("PointsCount");
    int i = 0;
    for ( ; i < stop; i++) {
        reader.readElement("Point");
        Base::Vector3d p;
        p.x = reader.getAttributeAsFloat("X");
        p.y = reader.getAttributeAsFloat("Y");
        p.z = reader.getAttributeAsFloat("Z");
        points.push_back(p);
    }
    reader.readEndElement("Points");
}

Base::Vector3d Generic::asVector()
{
    return getEndPoint() - getStartPoint();
}

double Generic::slope()
{
    Base::Vector3d v = asVector();
    if (v.x == 0.0) {
        return DOUBLE_MAX;
    } else {
        return v.y/v.x;
    }
}

Base::Vector3d Generic::apparentInter(GenericPtr g)
{
    Base::Vector3d dir0 = asVector();
    Base::Vector3d dir1 = g->asVector();

    // Line Intersetion (taken from ViewProviderSketch.cpp)
    double det = dir0.x*dir1.y - dir0.y*dir1.x;
    if ((det > 0 ? det : -det) < 1e-10)
        throw Base::ValueError("Invalid selection - Det = 0");

    double c0 = dir0.y*points.at(0).x - dir0.x*points.at(0).y;
    double c1 = dir1.y*g->points.at(1).x - dir1.x*g->points.at(1).y;
    double x = (dir0.x*c1 - dir1.x*c0)/det;
    double y = (dir0.y*c1 - dir1.y*c0)/det;

    // Apparent Intersection point
    return Base::Vector3d(x, y, 0.0);
}

BSpline::BSpline(const TopoDS_Edge &e)
{
    geomType = BSPLINE;
    BRepAdaptor_Curve c(e);
    isArc = !c.IsClosed();
    Handle(Geom_BSplineCurve) cSpline = c.BSpline();
    occEdge = e;
    Handle(Geom_BSplineCurve) spline;

    double f, l;
    f = c.FirstParameter();
    l = c.LastParameter();
    gp_Pnt s = c.Value(f);
    gp_Pnt m = c.Value((l+f)/2.0);
    gp_Pnt ePt = c.Value(l);
    startPnt = Base::Vector3d(s.X(), s.Y(), s.Z());
    endPnt = Base::Vector3d(ePt.X(), ePt.Y(), ePt.Z());
    midPnt = Base::Vector3d(m.X(), m.Y(), m.Z());
    gp_Vec v1(m, s);
    gp_Vec v2(m, ePt);
    gp_Vec v3(0, 0, 1);
    double a = v3.DotCross(v1, v2);
    cw = (a < 0) ? true: false;

    startAngle = atan2(startPnt.y, startPnt.x);
    if (startAngle < 0) {
         startAngle += 2.0 * M_PI;
    }
    endAngle = atan2(endPnt.y, endPnt.x);
    if (endAngle < 0) {
         endAngle += 2.0 * M_PI;
    }

    Standard_Real tol3D = 0.001;                                   //1/1000 of a mm? screen can't resolve this
    Standard_Integer maxDegree = 3, maxSegment = 200;
    Handle(BRepAdaptor_HCurve) hCurve = new BRepAdaptor_HCurve(c);
    // approximate the curve using a tolerance
    //Approx_Curve3d approx(hCurve, tol3D, GeomAbs_C2, maxSegment, maxDegree);   //gives degree == 5  ==> too many poles ==> buffer overrun
    Approx_Curve3d approx(hCurve, tol3D, GeomAbs_C0, maxSegment, maxDegree);
    if (approx.IsDone() && approx.HasResult()) {
        spline = approx.Curve();
    }
    else if (approx.HasResult()) { //result, but not within tolerance
        spline = approx.Curve();
    }
    else {
        f = c.FirstParameter();
        l = c.LastParameter();
        s = c.Value(f);
        ePt = c.Value(l);
        TColgp_Array1OfPnt controlPoints(0, 1);
        controlPoints.SetValue(0, s);
        controlPoints.SetValue(1, ePt);
        spline = GeomAPI_PointsToBSpline(controlPoints, 1).Curve();
    }

    GeomConvert_BSplineCurveToBezierCurve crt(spline);

    gp_Pnt controlPoint;
    for (Standard_Integer i = 1; i <= crt.NbArcs(); ++i) {
        BezierSegment tempSegment;
        Handle(Geom_BezierCurve) bezier = crt.Arc(i);
        tempSegment.poles = bezier->NbPoles();
        tempSegment.degree = bezier->Degree();
        for (int pole = 1; pole <= tempSegment.poles; ++pole) {
            controlPoint = bezier->Pole(pole);
            tempSegment.pnts.emplace_back(controlPoint.X(), controlPoint.Y(), controlPoint.Z());
        }
        segments.push_back(tempSegment);
    }
    if (e.Orientation() == TopAbs_REVERSED) {
        reversed = true;
    }
}


//! Can this B-spline be represented by a straight line?
// if len(first-last) == sum(len(pi - pi+1)) then it is a line
bool BSpline::isLine()
{
    return GeometryUtils::isLine(occEdge);
}

//used by DVDim for approximate dims
bool BSpline::isCircle()
{
    return GeometryUtils::isCircle(occEdge);
}

// make a circular edge from B-spline
TopoDS_Edge BSpline::asCircle(bool& arc)
{
    return GeometryUtils::asCircle(occEdge, arc);
}

bool BSpline::intersectsArc(Base::Vector3d p1, Base::Vector3d p2)
{
    gp_Pnt pnt1(p1.x, p1.y,p1.z);
    TopoDS_Vertex v1 = BRepBuilderAPI_MakeVertex(pnt1);
    gp_Pnt pnt2(p2.x, p2.y, p2.z);
    TopoDS_Vertex v2 = BRepBuilderAPI_MakeVertex(pnt2);
    BRepBuilderAPI_MakeEdge mkEdge(v1, v2);
    TopoDS_Edge line = mkEdge.Edge();
    BRepExtrema_DistShapeShape extss(occEdge, line);
    if (!extss.IsDone() || extss.NbSolution() == 0) {
        return false;
    }
    double minDist = extss.Value();
    if (minDist < Precision::Confusion()) {
        return true;
    }
    return false;
}


BezierSegment::BezierSegment(const TopoDS_Edge &e)
{
    geomType = BEZIER;
    occEdge = e;
    BRepAdaptor_Curve c(e);
    Handle(Geom_BezierCurve) bez = c.Bezier();
    poles = bez->NbPoles();
    degree = bez->Degree();
    for (int i = 1; i <= poles; ++i) {
        gp_Pnt controlPoint = bez->Pole(i);
        pnts.emplace_back(controlPoint.X(), controlPoint.Y(), controlPoint.Z());
    }
    if (e.Orientation() == TopAbs_REVERSED) {
        reversed = true;
    }
}

//**** Vertex
Vertex::Vertex()
{
    pnt = Base::Vector3d(0.0, 0.0, 0.0);
    extractType = ExtractionType::Plain;       //obs?
    hlrVisible = false;
    ref3D = -1;                        //obs. never used.
    m_center = false;
    BRepBuilderAPI_MakeVertex mkVert(gp_Pnt(0.0, 0.0, 0.0));
    occVertex = mkVert.Vertex();
    cosmetic = false;
    cosmeticLink = -1;
    cosmeticTag = std::string();
    m_reference = false;
    createNewTag();
}

Vertex::Vertex(const Vertex* v)
{
    pnt = v->point();
    extractType = v->extractType;       //obs?
    hlrVisible = v->hlrVisible;
    ref3D = v->ref3D;                  //obs. never used.
    m_center = v->m_center;
    occVertex = v->occVertex;
    cosmetic = v->cosmetic;
    cosmeticLink = v->cosmeticLink;
    cosmeticTag = v->cosmeticTag;
    m_reference = false;
    createNewTag();
}

Vertex::Vertex(double x, double y)
{
    pnt = Base::Vector3d(x, y, 0.0);
    extractType = ExtractionType::Plain;       //obs?
    hlrVisible = false;
    ref3D = -1;                        //obs. never used.
    m_center = false;
    BRepBuilderAPI_MakeVertex mkVert(gp_Pnt(x, y, 0.0));
    occVertex = mkVert.Vertex();
    cosmetic = false;
    cosmeticLink = -1;
    cosmeticTag = std::string();
    m_reference = false;
    createNewTag();
}

Vertex::Vertex(Base::Vector3d v) : Vertex(v.x, v.y)
{
//    Base::Console().Message("V::V(%s)\n",
//                            DrawUtil::formatVector(v).c_str());
}


bool Vertex::isEqual(const Vertex& v, double tol)
{
    double dist = (pnt - (v.pnt)).Length();
    if (dist <= tol) {
        return true;
    }
    return false;
}

void Vertex::Save(Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Point "
                << "X=\"" <<  pnt.x <<
                "\" Y=\"" <<  pnt.y <<
                "\" Z=\"" <<  pnt.z <<
                 "\"/>" << endl;

    writer.Stream() << writer.ind() << "<Extract value=\"" <<  extractType << "\"/>" << endl;
    const char v = hlrVisible ? '1':'0';
    writer.Stream() << writer.ind() << "<HLRVisible value=\"" <<  v << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<Ref3D value=\"" <<  ref3D << "\"/>" << endl;
    const char c = m_center ?'1':'0';
    writer.Stream() << writer.ind() << "<IsCenter value=\"" <<  c << "\"/>" << endl;
    const char c2 = cosmetic?'1':'0';
    writer.Stream() << writer.ind() << "<Cosmetic value=\"" <<  c2 << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<CosmeticLink value=\"" <<  cosmeticLink << "\"/>" << endl;
    writer.Stream() << writer.ind() << "<CosmeticTag value=\"" <<  cosmeticTag << "\"/>" << endl;

    //do we need to save this?  always recreated by program.
//    const char r = reference?'1':'0';
//    writer.Stream() << writer.ind() << "<Reference value=\"" <<  r << "\"/>" << endl;

    writer.Stream() << writer.ind() << "<VertexTag value=\"" <<  getTagAsString() << "\"/>" << endl;
}

void Vertex::Restore(Base::XMLReader &reader)
{
    reader.readElement("Point");
    pnt.x = reader.getAttributeAsFloat("X");
    pnt.y = reader.getAttributeAsFloat("Y");
    pnt.z = reader.getAttributeAsFloat("Z");

    reader.readElement("Extract");
    extractType = static_cast<ExtractionType>(reader.getAttributeAsInteger("value"));
//    reader.readElement("Visible");
//    hlrVisible = (bool)reader.getAttributeAsInteger("value")==0?false:true;
    reader.readElement("Ref3D");
    ref3D = reader.getAttributeAsInteger("value");
    reader.readElement("IsCenter");
    hlrVisible = reader.getAttributeAsInteger("value") != 0;
    reader.readElement("Cosmetic");
    cosmetic = reader.getAttributeAsInteger("value") != 0;
    reader.readElement("CosmeticLink");
    cosmeticLink = reader.getAttributeAsInteger("value");
    reader.readElement("CosmeticTag");
    cosmeticTag = reader.getAttribute("value");

    //will restore read to eof looking for "Reference" in old docs??  YES!!
//    reader.readElement("Reference");
//    m_reference = (bool)reader.getAttributeAsInteger("value")==0?false:true;

    reader.readElement("VertexTag");
    std::string temp = reader.getAttribute("value");
    boost::uuids::string_generator gen;
    boost::uuids::uuid u1 = gen(temp);
    tag = u1;

    BRepBuilderAPI_MakeVertex mkVert(gp_Pnt(pnt.x, pnt.y, pnt.z));
    occVertex = mkVert.Vertex();
}

void Vertex::createNewTag()
{
    // Initialize a random number generator, to avoid Valgrind false positives.
    // The random number generator is not threadsafe so we guard it.  See
    // https://www.boost.org/doc/libs/1_62_0/libs/uuid/uuid.html#Design%20notes
    static boost::mt19937 ran;
    static bool seeded = false;
    static boost::mutex random_number_mutex;

    boost::lock_guard<boost::mutex> guard(random_number_mutex);
    if (!seeded) {
        ran.seed(static_cast<unsigned int>(std::time(nullptr)));
        seeded = true;
    }
    static boost::uuids::basic_random_generator<boost::mt19937> gen(&ran);

    tag = gen();
}


boost::uuids::uuid Vertex::getTag() const
{
    return tag;
}

std::string Vertex::getTagAsString() const
{
    return boost::uuids::to_string(getTag());
}

void Vertex::dump(const char* title)
{
    Base::Console().Message("TD::Vertex - %s - point: %s vis: %d cosmetic: %d  cosLink: %d cosTag: %s\n",
                            title, DrawUtil::formatVector(pnt).c_str(), hlrVisible, cosmetic, cosmeticLink,
                            cosmeticTag.c_str());
}

TopoShape Vertex::asTopoShape(double scale)
{
    Base::Vector3d point = DU::toVector3d(BRep_Tool::Pnt(getOCCVertex()));
    point = point / scale;
    BRepBuilderAPI_MakeVertex mkVert(DU::to<gp_Pnt>(point));
    return TopoShape(mkVert.Vertex());
}


/*static*/
BaseGeomPtrVector GeometryUtils::chainGeoms(BaseGeomPtrVector geoms)
{
    BaseGeomPtrVector result;
    std::vector<bool> used(geoms.size(), false);

    if (geoms.empty()) {
        return result;
    }

    if (geoms.size() == 1) {
        //don't bother for single geom (circles, ellipses, etc)
        result.push_back(geoms[0]);
    } else {
        //start with first edge
        result.push_back(geoms[0]);
        Base::Vector3d atPoint = (geoms[0])->getEndPoint();
        used[0] = true;
        for (unsigned int i = 1; i < geoms.size(); i++) { //do size-1 more edges
            auto next( nextGeom(atPoint, geoms, used, Precision::Confusion()) );
            if (next.index) { //found an unused edge with vertex == atPoint
                BaseGeomPtr nextEdge = geoms.at(next.index);
                used[next.index] = true;
                nextEdge->setReversed(next.reversed);
                result.push_back(nextEdge);
                if (next.reversed) {
                    atPoint = nextEdge->getStartPoint();
                } else {
                    atPoint = nextEdge->getEndPoint();
                }
            }
        }
    }
    return result;
}


/*static*/ GeometryUtils::ReturnType GeometryUtils::nextGeom(
        Base::Vector3d atPoint,
        BaseGeomPtrVector geoms,
        std::vector<bool> used,
        double tolerance )
{
    ReturnType result(0, false);
    auto index(0);
    for (auto itGeom : geoms) {
        if (used[index]) {
            ++index;
            continue;
        }
        if ((atPoint - itGeom->getStartPoint()).Length() < tolerance) {
            result.index = index;
            result.reversed = false;
            break;
        } else if ((atPoint - itGeom->getEndPoint()).Length() < tolerance) {
            result.index = index;
            result.reversed = true;
            break;
        }
        ++index;
    }
    return result;
}

TopoDS_Edge GeometryUtils::edgeFromGeneric(TechDraw::GenericPtr g)
{
//    Base::Console().Message("GU::edgeFromGeneric()\n");
    //TODO: note that this isn't quite right as g can be a polyline!
    //sb points.first, points.last
    //and intermediates should be added to Point
    Base::Vector3d first = g->points.front();
    Base::Vector3d last  = g->points.back();
    gp_Pnt gp1(first.x, first.y, first.z);
    gp_Pnt gp2(last.x, last.y, last.z);
    return BRepBuilderAPI_MakeEdge(gp1, gp2);
}

TopoDS_Edge GeometryUtils::edgeFromCircle(TechDraw::CirclePtr c)
{
    gp_Pnt loc(c->center.x, c->center.y, c->center.z);
    gp_Dir dir(0, 0, 1);
    gp_Ax1 axis(loc, dir);
    gp_Circ circle;
    circle.SetAxis(axis);
    circle.SetRadius(c->radius);
    Handle(Geom_Circle) hCircle = new Geom_Circle (circle);
    BRepBuilderAPI_MakeEdge aMakeEdge(hCircle, 0.0, 2.0 * M_PI);
    return aMakeEdge.Edge();
}

TopoDS_Edge GeometryUtils::edgeFromCircleArc(TechDraw::AOCPtr c)
{
    gp_Pnt loc(c->center.x, c->center.y, c->center.z);
    gp_Dir dir(0, 0, 1);
    gp_Ax1 axis(loc, dir);
    gp_Circ circle;
    circle.SetAxis(axis);
    circle.SetRadius(c->radius);
    Handle(Geom_Circle) hCircle = new Geom_Circle (circle);
    double startAngle = c->startAngle;
    double endAngle = c->endAngle;
    BRepBuilderAPI_MakeEdge aMakeEdge(hCircle, startAngle, endAngle);
    return aMakeEdge.Edge();
}

//used by DVDim for approximate dims
bool GeometryUtils::isCircle(TopoDS_Edge occEdge)
{
    double radius;
    Base::Vector3d center;
    bool isArc = false;
    return GeometryUtils::getCircleParms(occEdge, radius, center, isArc);
}

//! tries to interpret a B-spline edge as a circle. Used by DVDim for approximate dimensions.
//! calculates the curvature of the spline at a number of places and measures the deviation from the average
//! a true circle has constant curvature and would have no deviation from the average.
bool GeometryUtils::getCircleParms(TopoDS_Edge occEdge, double& radius, Base::Vector3d& center, bool& isArc)
{
    int testCount = 5;
    double curveLimit = EWTOLERANCE;
    BRepAdaptor_Curve c(occEdge);
    Handle(Geom_BSplineCurve) spline = c.BSpline();
    double f, l;
    f = c.FirstParameter();
    l = c.LastParameter();
    double parmRange = fabs(l - f);
    double parmStep = parmRange/testCount;
    std::vector<double> curvatures;
    std::vector<gp_Pnt> centers;
    gp_Pnt curveCenter;
    double sumCurvature = 0;
    Base::Vector3d sumCenter, valueAt;
    try {
        GeomLProp_CLProps prop(spline, f, 3, Precision::Confusion());

        // check only the interior points of the edge
        for (int i = 1; i < (testCount - 1); i++) {
            prop.SetParameter(parmStep * i);
            curvatures.push_back(prop.Curvature());
            sumCurvature += prop.Curvature();
            prop.CentreOfCurvature(curveCenter);
            centers.push_back(curveCenter);
            sumCenter += DrawUtil::toVector3d(curveCenter);
        }

    }
    catch (Standard_Failure&) {
        return false;
    }
    Base::Vector3d avgCenter = sumCenter/ centers.size();

    double avgCurve = sumCurvature/ centers.size();
    double errorCurve  = 0;
    // sum the errors in curvature
    for (auto& cv: curvatures) {
        errorCurve += avgCurve - cv;
    }

    double errorCenter{0};
    for (auto& observe : centers) {
        auto error = (DU::toVector3d(observe)- avgCenter).Length();
        errorCenter += error;
    }

    // calculate average error in curvature.  we are only interested in the magnitude of the error
    errorCurve  = fabs(errorCurve / curvatures.size());
    // calculate the average error in center of curvature
    errorCenter = errorCenter / curvatures.size();
    auto edgeLong = edgeLength(occEdge);
    double centerLimit = edgeLong * 0.01;

    isArc = !c.IsClosed();
    bool isCircle(false);
    if ( errorCurve <= curveLimit  &&
         errorCenter <= centerLimit) {
        isCircle = true;
        radius = 1.0/avgCurve;
        center = avgCenter;
    }

    return isCircle;
}

//! make a circle or arc of circle Edge from BSpline Edge
// Note that the input edge has been inverted by GeometryObject, so +Y points down.
TopoDS_Edge GeometryUtils::asCircle(TopoDS_Edge splineEdge, bool& arc)
{
    double radius{0};
    Base::Vector3d center;
    bool isArc = false;
    bool canMakeCircle = GeometryUtils::getCircleParms(splineEdge, radius, center, isArc);
    if (!canMakeCircle) {
        throw Base::RuntimeError("GU::asCircle received non-circular edge!");
    }

    gp_Pnt gCenter = DU::to<gp_Pnt>(center);
    gp_Dir gNormal{0, 0, 1};
    Handle(Geom_Circle) circleFromParms = GC_MakeCircle(gCenter, gNormal, radius);

    // find the ends of the edge from the underlying curve
    BRepAdaptor_Curve curveAdapt(splineEdge);
    double firstParam = curveAdapt.FirstParameter();
    double lastParam = curveAdapt.LastParameter();
    gp_Pnt startPoint = curveAdapt.Value(firstParam);
    gp_Pnt endPoint = curveAdapt.Value(lastParam);

    if (startPoint.IsEqual(endPoint, EWTOLERANCE)) {    //more reliable than IsClosed flag
        arc = false;
        return BRepBuilderAPI_MakeEdge(circleFromParms);
    }

    arc = true;
    double midRange = (lastParam + firstParam) / 2;
    gp_Pnt midPoint = curveAdapt.Value(midRange);

    GC_MakeArcOfCircle mkArc(startPoint, midPoint, endPoint);
    auto circleArc = mkArc.Value();

    return BRepBuilderAPI_MakeEdge(circleArc);
}


bool GeometryUtils::isLine(TopoDS_Edge occEdge)
{
    BRepAdaptor_Curve c(occEdge);

    Handle(Geom_BSplineCurve) spline = c.BSpline();
    double f = c.FirstParameter();
    double l = c.LastParameter();
    gp_Pnt s = c.Value(f);
    gp_Pnt e = c.Value(l);

    bool samePnt = s.IsEqual(e, FLT_EPSILON);
    if (samePnt) {
        return false;
    }

    Base::Vector3d vs = DrawUtil::toVector3d(s);
    Base::Vector3d ve = DrawUtil::toVector3d(e);
    double endLength = (vs - ve).Length();
    int low = 0;
    int high = spline->NbPoles() - 1;
    TColgp_Array1OfPnt poles(low, high);
    spline->Poles(poles);
    double lenTotal = 0.0;
    for (int i = 0; i < high; i++) {
        gp_Pnt p1 = poles(i);
        Base::Vector3d v1 = DrawUtil::toVector3d(p1);
        gp_Pnt p2 = poles(i+1);
        Base::Vector3d v2 = DrawUtil::toVector3d(p2);
        lenTotal += (v2-v1).Length();
    }

    if (DrawUtil::fpCompare(lenTotal, endLength)) {
        return true;
    }
    return false;
}


//! make a line Edge from B-spline Edge
TopoDS_Edge GeometryUtils::asLine(TopoDS_Edge occEdge)
{
    BRepAdaptor_Curve c(occEdge);

    // find the two ends
    Handle(Geom_Curve) curve = c.Curve().Curve();
    double first = c.FirstParameter();
    double last = c.LastParameter();
    gp_Pnt start = c.Value(first);
    gp_Pnt end = c.Value(last);

    TopoDS_Edge result = BRepBuilderAPI_MakeEdge(start, end);
    return result;
}


double GeometryUtils::edgeLength(TopoDS_Edge occEdge)
{
    BRepAdaptor_Curve adapt(occEdge);
    const Handle(Geom_Curve) curve = adapt.Curve().Curve();
    double first = BRepLProp_CurveTool::FirstParameter(adapt);
    double last = BRepLProp_CurveTool::LastParameter(adapt);
    try {
        GeomAdaptor_Curve adaptor(curve);
        return GCPnts_AbscissaPoint::Length(adaptor,first,last,Precision::Confusion());
    }
    catch (Standard_Failure& exc) {
        THROWM(Base::CADKernelError, exc.GetMessageString())
    }
}

//! return a perforated shape/face (using Part::FaceMakerCheese) formed by creating holes in the input face.
TopoDS_Face GeometryUtils::makePerforatedFace(FacePtr bigCheese,  const std::vector<FacePtr> &holesAll)
{
    std::vector<TopoDS_Wire> cheeseIngredients;

    // v0.0 brute force

    // Note: TD Faces are not perforated and should only ever have 1 wire.  They are capable of
    // having voids, but for now we will just take the first contour wire in all cases.

    if (bigCheese->wires.empty())  {
        // run in circles.  scream and shout.
        return {};
    }

    auto flippedFace = ShapeUtils::fromQtAsFace(bigCheese->toOccFace());

    if (holesAll.empty()) {
        return flippedFace;
    }

    auto outer = ShapeUtils::fromQtAsWire(bigCheese->wires.front()->toOccWire());
    cheeseIngredients.push_back(outer);
    for (auto& hole : holesAll) {
        if (hole->wires.empty()) {
            continue;
        }
        auto holeR3 = ShapeUtils::fromQtAsWire(hole->wires.front()->toOccWire());
        cheeseIngredients.push_back(holeR3);
    }

    TopoDS_Shape faceShape;
    try {
        faceShape = Part::FaceMakerCheese::makeFace(cheeseIngredients);
    }
    catch (const Standard_Failure &e) {
        Base::Console().Warning("Area - could not make holes in face\n");
        return flippedFace;
    }


    // v0.0 just grab the first face
    TopoDS_Face foundFace;
    TopExp_Explorer expFaces(faceShape, TopAbs_FACE);
    if (expFaces.More()) {
        foundFace = TopoDS::Face(expFaces.Current());
    }
    // TODO: sort out the compound => shape but !compound => face business in FaceMakerCheese here.
    //       first guess is it does not affect us?

    return foundFace;
}


//! find faces within the bounds of the input face
std::vector<FacePtr> GeometryUtils::findHolesInFace(const DrawViewPart* dvp, const std::string& bigCheeseSubRef)
{
    if (!dvp || bigCheeseSubRef.empty()) {
        return {};
    }

    std::vector<FacePtr> holes;
    auto bigCheeseIndex = DU::getIndexFromName(bigCheeseSubRef);

    // v0.0 brute force
    auto facesAll = dvp->getFaceGeometry();
    if (facesAll.empty()) {
        // tarfu
        throw Base::RuntimeError("GU::findHolesInFace - no holes to find!!");
    }

    auto bigCheeseFace = facesAll.at(bigCheeseIndex);
    auto bigCheeseOCCFace = bigCheeseFace->toOccFace();
    auto bigCheeseArea = bigCheeseFace->getArea();

    int iFace{0};
    for (auto& face : facesAll) {
        if (iFace == bigCheeseIndex) {
            iFace++;
            continue;
        }
        if (face->getArea() > bigCheeseArea) {
            iFace++;
            continue;
        }
        auto faceCenter = DU::to<gp_Pnt>(face->getCenter());
        auto faceCenterVertex = BRepBuilderAPI_MakeVertex(faceCenter);
        auto distance = DU::simpleMinDist(faceCenterVertex, bigCheeseOCCFace);
        if (distance > EWTOLERANCE) {
            // hole center not within outer contour.  not the best test but cheese maker handles it
            // for us?
            // FaceMakerCheese does not support partial overlaps and just ignores them?
            iFace++;
            continue;
        }
        holes.push_back(face);
        iFace++;
    }

    return holes;
}

