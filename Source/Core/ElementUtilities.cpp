/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "../../Include/RmlUi/Core/ElementUtilities.h"
#include "../../Include/RmlUi/Core/Context.h"
#include "../../Include/RmlUi/Core/Core.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/ElementScroll.h"
#include "../../Include/RmlUi/Core/Factory.h"
#include "../../Include/RmlUi/Core/FontEngineInterface.h"
#include "../../Include/RmlUi/Core/RenderInterface.h"
#include "../../Include/RmlUi/Core/TextShapingContext.h"
#include "DataController.h"
#include "DataModel.h"
#include "DataView.h"
#include "ElementStyle.h"
#include "Layout/LayoutDetails.h"
#include "Layout/LayoutEngine.h"
#include "TransformState.h"
#include <limits>

namespace Rml {

// Builds and sets the box for an element.
static void SetBox(Element* element)
{
	Element* parent = element->GetParentNode();
	RMLUI_ASSERT(parent != nullptr);

	Vector2f containing_block = parent->GetBox().GetSize();
	containing_block.x -= parent->GetElementScroll()->GetScrollbarSize(ElementScroll::VERTICAL);
	containing_block.y -= parent->GetElementScroll()->GetScrollbarSize(ElementScroll::HORIZONTAL);

	Box box;
	LayoutDetails::BuildBox(box, containing_block, element);

	if (element->GetComputedValues().height().type != Style::Height::Auto)
		box.SetContent(Vector2f(box.GetSize().x, containing_block.y));

	element->SetBox(box);
}

// Positions an element relative to an offset parent.
static void SetElementOffset(Element* element, Vector2f offset)
{
	Vector2f relative_offset = element->GetParentNode()->GetBox().GetPosition(BoxArea::Content);
	relative_offset += offset;
	relative_offset.x += element->GetBox().GetEdge(BoxArea::Margin, BoxEdge::Left);
	relative_offset.y += element->GetBox().GetEdge(BoxArea::Margin, BoxEdge::Top);

	element->SetOffset(relative_offset, element->GetParentNode());
}

Element* ElementUtilities::GetElementById(Element* root_element, const String& id)
{
	// Breadth first search on elements for the corresponding id
	typedef Queue<Element*> SearchQueue;
	SearchQueue search_queue;
	search_queue.push(root_element);

	while (!search_queue.empty())
	{
		Element* element = search_queue.front();
		search_queue.pop();

		if (element->GetId() == id)
		{
			return element;
		}

		// Add all children to search
		for (int i = 0; i < element->GetNumChildren(); i++)
			search_queue.push(element->GetChild(i));
	}

	return nullptr;
}

void ElementUtilities::GetElementsByTagName(ElementList& elements, Element* root_element, const String& tag)
{
	// Breadth first search on elements for the corresponding id
	typedef Queue<Element*> SearchQueue;
	SearchQueue search_queue;
	for (int i = 0; i < root_element->GetNumChildren(); ++i)
		search_queue.push(root_element->GetChild(i));

	while (!search_queue.empty())
	{
		Element* element = search_queue.front();
		search_queue.pop();

		if (element->GetTagName() == tag)
			elements.push_back(element);

		// Add all children to search.
		for (int i = 0; i < element->GetNumChildren(); i++)
			search_queue.push(element->GetChild(i));
	}
}

void ElementUtilities::GetElementsByClassName(ElementList& elements, Element* root_element, const String& class_name)
{
	// Breadth first search on elements for the corresponding id
	typedef Queue<Element*> SearchQueue;
	SearchQueue search_queue;
	for (int i = 0; i < root_element->GetNumChildren(); ++i)
		search_queue.push(root_element->GetChild(i));

	while (!search_queue.empty())
	{
		Element* element = search_queue.front();
		search_queue.pop();

		if (element->IsClassSet(class_name))
			elements.push_back(element);

		// Add all children to search.
		for (int i = 0; i < element->GetNumChildren(); i++)
			search_queue.push(element->GetChild(i));
	}
}

float ElementUtilities::GetDensityIndependentPixelRatio(Element* element)
{
	Context* context = element->GetContext();
	if (context == nullptr)
		return 1.0f;

	return context->GetDensityIndependentPixelRatio();
}

int ElementUtilities::GetStringWidth(Element* element, const String& string, Character prior_character)
{
	const auto& computed = element->GetComputedValues();
	const TextShapingContext text_shaping_context{ computed.language(), computed.direction(), computed.letter_spacing() };

	FontFaceHandle font_face_handle = element->GetFontFaceHandle();
	if (font_face_handle == 0)
		return 0;

	return GetFontEngineInterface()->GetStringWidth(font_face_handle, string, text_shaping_context, prior_character);
}

bool ElementUtilities::GetClippingRegion(Vector2i& clip_origin, Vector2i& clip_dimensions, Element* element)
{
	using Style::Clip;
	clip_origin = Vector2i(-1, -1);
	clip_dimensions = Vector2i(-1, -1);

	Clip target_element_clip = element->GetComputedValues().clip();
	if (target_element_clip == Clip::Type::None)
		return false;

	int num_ignored_clips = target_element_clip.GetNumber();

	// Search through the element's ancestors, finding all elements that clip their overflow and have overflow to clip.
	// For each that we find, we combine their clipping region with the existing clipping region, and so build up a
	// complete clipping region for the element.
	Element* clipping_element = element->GetOffsetParent();

	while (clipping_element != nullptr)
	{
		const ComputedValues& clip_computed = clipping_element->GetComputedValues();
		const bool clip_enabled = (clip_computed.overflow_x() != Style::Overflow::Visible || clip_computed.overflow_y() != Style::Overflow::Visible);
		const bool clip_always = (clip_computed.clip() == Clip::Type::Always);
		const bool clip_none = (clip_computed.clip() == Clip::Type::None);
		const int clip_number = clip_computed.clip().GetNumber();

		// Merge the existing clip region with the current clip region if we aren't ignoring clip regions.
		if ((clip_always || clip_enabled) && num_ignored_clips == 0)
		{
			// Ignore nodes that don't clip.
			if (clip_always || clipping_element->GetClientWidth() < clipping_element->GetScrollWidth() - 0.5f ||
				clipping_element->GetClientHeight() < clipping_element->GetScrollHeight() - 0.5f)
			{
				const BoxArea client_area = clipping_element->GetClientArea();
				Vector2f element_origin_f = clipping_element->GetAbsoluteOffset(client_area);
				Vector2f element_dimensions_f = clipping_element->GetBox().GetSize(client_area);
				Math::SnapToPixelGrid(element_origin_f, element_dimensions_f);

				const Vector2i element_origin(element_origin_f);
				const Vector2i element_dimensions(element_dimensions_f);

				if (clip_origin == Vector2i(-1, -1) && clip_dimensions == Vector2i(-1, -1))
				{
					clip_origin = element_origin;
					clip_dimensions = element_dimensions;
				}
				else
				{
					const Vector2i top_left = Math::Max(clip_origin, element_origin);
					const Vector2i bottom_right = Math::Min(clip_origin + clip_dimensions, element_origin + element_dimensions);

					clip_origin = top_left;
					clip_dimensions = Math::Max(Vector2i(0), bottom_right - top_left);
				}
			}
		}

		// If this region is meant to clip and we're skipping regions, update the counter.
		if (num_ignored_clips > 0 && clip_enabled)
			num_ignored_clips--;

		// Inherit how many clip regions this ancestor ignores.
		num_ignored_clips = Math::Max(num_ignored_clips, clip_number);

		// If this region ignores all clipping regions, then we do too.
		if (clip_none)
			break;

		// Climb the tree to this region's parent.
		clipping_element = clipping_element->GetOffsetParent();
	}

	return clip_dimensions.x >= 0 && clip_dimensions.y >= 0;
}

bool ElementUtilities::SetClippingRegion(Element* element, Context* context)
{
	if (element && !context)
		context = element->GetContext();

	if (!context)
		return false;

	Vector2i clip_origin = {-1, -1};
	Vector2i clip_dimensions = {-1, -1};
	bool clip = element && GetClippingRegion(clip_origin, clip_dimensions, element);

	Vector2i current_origin = {-1, -1};
	Vector2i current_dimensions = {-1, -1};
	bool current_clip = context->GetActiveClipRegion(current_origin, current_dimensions);
	if (current_clip != clip || (clip && (clip_origin != current_origin || clip_dimensions != current_dimensions)))
	{
		context->SetActiveClipRegion(clip_origin, clip_dimensions);
		ApplyActiveClipRegion(context);
	}

	return true;
}

void ElementUtilities::ApplyActiveClipRegion(Context* context)
{
	RenderInterface* render_interface = ::Rml::GetRenderInterface();
	if (!render_interface)
		return;

	Vector2i origin;
	Vector2i dimensions;
	bool clip_enabled = context->GetActiveClipRegion(origin, dimensions);

	render_interface->EnableScissorRegion(clip_enabled);
	if (clip_enabled)
	{
		render_interface->SetScissorRegion(origin.x, origin.y, dimensions.x, dimensions.y);
	}
}

void ElementUtilities::FormatElement(Element* element, Vector2f containing_block)
{
	LayoutEngine::FormatElement(element, containing_block);
}

void ElementUtilities::BuildBox(Box& box, Vector2f containing_block, Element* element, bool inline_element)
{
	LayoutDetails::BuildBox(box, containing_block, element, inline_element ? BuildBoxMode::Inline : BuildBoxMode::Block);
}

bool ElementUtilities::PositionElement(Element* element, Vector2f offset, PositionAnchor anchor)
{
	Element* parent = element->GetParentNode();
	if (parent == nullptr)
		return false;

	SetBox(element);

	Vector2f containing_block = element->GetParentNode()->GetBox().GetSize(BoxArea::Content);
	Vector2f element_block = element->GetBox().GetSize(BoxArea::Margin);

	Vector2f resolved_offset = offset;

	if (anchor & RIGHT)
		resolved_offset.x = containing_block.x - (element_block.x + offset.x);

	if (anchor & BOTTOM)
		resolved_offset.y = containing_block.y - (element_block.y + offset.y);

	SetElementOffset(element, resolved_offset);

	return true;
}

bool ElementUtilities::ApplyTransform(Element& element)
{
	RenderInterface* render_interface = ::Rml::GetRenderInterface();
	if (!render_interface)
		return false;

	static const Matrix4f* old_transform_ptr = {}; // This may be expired, dereferencing not allowed!
	static Matrix4f old_transform_value = Matrix4f::Identity();

	const Matrix4f* new_transform_ptr = nullptr;
	if (const TransformState* state = element.GetTransformState())
		new_transform_ptr = state->GetTransform();

	// Only changed transforms are submitted.
	if (old_transform_ptr != new_transform_ptr)
	{
		// Do a deep comparison as well to avoid submitting a new transform which is equal.
		if (!old_transform_ptr || !new_transform_ptr || (old_transform_value != *new_transform_ptr))
		{
			render_interface->SetTransform(new_transform_ptr);

			if (new_transform_ptr)
				old_transform_value = *new_transform_ptr;
		}

		old_transform_ptr = new_transform_ptr;
	}

	return true;
}

static bool ApplyDataViewsControllersInternal(Element* element, const bool construct_structural_view, const String& structural_view_inner_rml)
{
	RMLUI_ASSERT(element);
	bool result = false;

	// If we have an active data model, check the attributes for any data bindings
	if (DataModel* data_model = element->GetDataModel())
	{
		struct ViewControllerInitializer {
			String type;
			String modifier_or_inner_rml;
			String expression;
			DataViewPtr view;
			DataControllerPtr controller;
			explicit operator bool() const { return view || controller; }
		};

		// Since data views and controllers may modify the element's attributes during initialization, we
		// need to iterate over all the attributes _before_ initializing any views or controllers. We store
		// the information needed to initialize them in the following container.
		Vector<ViewControllerInitializer> initializer_list;

		for (auto& attribute : element->GetAttributes())
		{
			// Data views and controllers are declared by the following element attribute:
			//     data-[type]-[modifier]="[expression]"

			constexpr size_t data_str_length = sizeof("data-") - 1;

			const String& name = attribute.first;

			if (name.size() > data_str_length && name[0] == 'd' && name[1] == 'a' && name[2] == 't' && name[3] == 'a' && name[4] == '-')
			{
				const size_t type_end = name.find('-', data_str_length);
				const size_t type_size = (type_end == String::npos ? String::npos : type_end - data_str_length);
				String type_name = name.substr(data_str_length, type_size);

				ViewControllerInitializer initializer;

				// Structural data views are applied in a separate step from the normal views and controllers.
				if (construct_structural_view)
				{
					if (DataViewPtr view = Factory::InstanceDataView(type_name, element, true))
					{
						initializer.modifier_or_inner_rml = structural_view_inner_rml;
						initializer.view = std::move(view);
					}
				}
				else
				{
					if (Factory::IsStructuralDataView(type_name))
					{
						// Structural data views should cancel all other non-structural data views and controllers. Exit now.
						// Eg. in elements with a 'data-for' attribute, the data views should be constructed on the generated
						// children elements and not on the current element generating the 'for' view.
						return false;
					}

					const size_t modifier_offset = data_str_length + type_name.size() + 1;
					if (modifier_offset < name.size())
						initializer.modifier_or_inner_rml = name.substr(modifier_offset);

					if (DataViewPtr view = Factory::InstanceDataView(type_name, element, false))
						initializer.view = std::move(view);

					if (DataControllerPtr controller = Factory::InstanceDataController(type_name, element))
						initializer.controller = std::move(controller);
				}

				if (initializer)
				{
					initializer.type = std::move(type_name);
					initializer.expression = attribute.second.Get<String>();

					initializer_list.push_back(std::move(initializer));
				}
			}
		}

		// Now, we can safely initialize the data views and controllers, even modifying the element's attributes when desired.
		for (ViewControllerInitializer& initializer : initializer_list)
		{
			DataViewPtr& view = initializer.view;
			DataControllerPtr& controller = initializer.controller;

			if (view)
			{
				if (view->Initialize(*data_model, element, initializer.expression, initializer.modifier_or_inner_rml))
				{
					data_model->AddView(std::move(view));
					result = true;
				}
				else
					Log::Message(Log::LT_WARNING, "Could not add data-%s view to element: %s", initializer.type.c_str(),
						element->GetAddress().c_str());
			}

			if (controller)
			{
				if (controller->Initialize(*data_model, element, initializer.expression, initializer.modifier_or_inner_rml))
				{
					data_model->AddController(std::move(controller));
					result = true;
				}
				else
					Log::Message(Log::LT_WARNING, "Could not add data-%s controller to element: %s", initializer.type.c_str(),
						element->GetAddress().c_str());
			}
		}
	}

	return result;
}

bool ElementUtilities::ApplyDataViewsControllers(Element* element)
{
	return ApplyDataViewsControllersInternal(element, false, String());
}

bool ElementUtilities::ApplyStructuralDataViews(Element* element, const String& inner_rml)
{
	return ApplyDataViewsControllersInternal(element, true, inner_rml);
}

} // namespace Rml
