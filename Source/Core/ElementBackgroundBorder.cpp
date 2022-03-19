/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
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

#include "ElementBackgroundBorder.h"
#include "../../Include/RmlUi/Core/Box.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/GeometryUtilities.h"

namespace Rml {

ElementBackgroundBorder::ElementBackgroundBorder(Element* element) : geometry(element) {}

ElementBackgroundBorder::~ElementBackgroundBorder()
{
	if (shadow_texture || shadow_geometry)
	{
		if (Rml::RenderInterface* render_interface = geometry.GetRenderInterface())
		{
			render_interface->ReleaseCompiledGeometry(shadow_geometry);
			render_interface->ReleaseTexture(shadow_texture);
		}
	}
}

void ElementBackgroundBorder::Render(Element* element)
{
	if (background_dirty || border_dirty)
	{
		GenerateGeometry(element);

		background_dirty = false;
		border_dirty = false;
	}

	if (shadow_geometry)
	{
		RenderInterface* render_interface = element->GetRenderInterface();
		if (!render_interface)
		{
			RMLUI_ERROR;
			return;
		}

		// TODO: Render texture
		// render_interface->RenderEffect(shadow_texture, RenderStage::Decoration, 0, element);
		render_interface->EnableScissorRegion(false);
		// ElementUtilities::ApplyTransform(*element->GetContext()->GetRootElement());
		render_interface->RenderCompiledGeometry(shadow_geometry, element->GetAbsoluteOffset(Box::BORDER));
		// ElementUtilities::ApplyTransform(*element);
		ElementUtilities::ApplyActiveClipRegion(element->GetContext(), render_interface);
	}
	else if (geometry)
	{
		geometry.Render(element->GetAbsoluteOffset(Box::BORDER));
	}
}

void ElementBackgroundBorder::DirtyBackground()
{
	background_dirty = true;
}

void ElementBackgroundBorder::DirtyBorder()
{
	border_dirty = true;
}

void ElementBackgroundBorder::GenerateGeometry(Element* element)
{
	const ComputedValues& computed = element->GetComputedValues();

	Colourb background_color = computed.background_color;
	Colourb border_colors[4] = {
		computed.border_top_color,
		computed.border_right_color,
		computed.border_bottom_color,
		computed.border_left_color,
	};

	// Apply opacity
	const float opacity = computed.opacity;
	background_color.alpha = (byte)(opacity * (float)background_color.alpha);

	if (opacity < 1)
	{
		for (int i = 0; i < 4; ++i)
			border_colors[i].alpha = (byte)(opacity * (float)border_colors[i].alpha);
	}

	geometry.GetVertices().clear();
	geometry.GetIndices().clear();

	const Vector4f radii(computed.border_top_left_radius, computed.border_top_right_radius, computed.border_bottom_right_radius,
		computed.border_bottom_left_radius);

	for (int i = 0; i < element->GetNumBoxes(); i++)
	{
		Vector2f offset;
		const Box& box = element->GetBox(i, offset);
		GeometryUtilities::GenerateBackgroundBorder(&geometry, box, offset, radii, background_color, border_colors);
	}

	geometry.Release();

	if (shadow_texture || shadow_geometry)
	{
		if (RenderInterface* render_interface = element->GetRenderInterface())
		{
			render_interface->ReleaseCompiledGeometry(shadow_geometry);
			render_interface->ReleaseTexture(shadow_texture);
		}
	}

	if (const Property* p_box_shadow = element->GetLocalProperty(PropertyId::BoxShadow))
	{
		RMLUI_ASSERT(p_box_shadow->value.GetType() == Variant::SHADOWLIST);
		const ShadowList& shadow_list = p_box_shadow->value.GetReference<ShadowList>();

		RenderInterface* render_interface = element->GetRenderInterface();
		if (!render_interface)
		{
			RMLUI_ERROR;
			return;
		}

		Geometry geometry_border, geometry_padding;
		Vector2f element_offset_in_texture;
		Vector2f texture_dimensions;

		{
			Vector2f extend_top_left;
			Vector2f extend_bottom_right;

			for (const Shadow& shadow : shadow_list)
			{
				if (!shadow.inset)
				{
					const float extend = shadow.blur_radius + shadow.spread_distance;
					extend_top_left = Math::Max(extend_top_left, Vector2f(extend) - shadow.offset);
					extend_bottom_right = Math::Max(extend_bottom_right, Vector2f(extend) + shadow.offset);
				}
			}

			Vector2f offset_min;
			Vector2f offset_max;

			// Generate border and padding geometry, and extend the texture to encompass any additional boxes.
			for (int i = 0; i < element->GetNumBoxes(); i++)
			{
				static const Colourb opaque_colors[4];
				static const Colourb transparent_color(0, 0, 0, 0);
				Vector2f offset;
				const Box& box = element->GetBox(i, offset);
				GeometryUtilities::GenerateBackgroundBorder(&geometry_padding, box, offset, radii, opaque_colors[0], nullptr);
				GeometryUtilities::GenerateBackgroundBorder(&geometry_border, box, offset, radii, transparent_color, opaque_colors);
				offset_min = Math::Min(offset_min, offset);
				offset_max = Math::Max(offset_max, offset);
			}

			auto RoundUp = [](Vector2f v) { return Vector2f(Math::RoundUpFloat(v.x), Math::RoundUpFloat(v.y)); };

			element_offset_in_texture = RoundUp(extend_top_left - offset_min);
			texture_dimensions = RoundUp(element_offset_in_texture + element->GetBox().GetSize(Box::BORDER) + extend_bottom_right + offset_max);
		}

		ElementUtilities::ApplyTransform(nullptr, render_interface);
		render_interface->EnableScissorRegion(true);
		render_interface->SetScissorRegion(0, 0, (int)texture_dimensions.x, (int)texture_dimensions.y);
		render_interface->ExecuteRenderCommand(RenderCommand::StackPush);

		constexpr int mask_padding = 0b001;
		constexpr int mask_border = 0b010;
		constexpr int mask_inset = 0b100;

		render_interface->StencilCommand(StencilCommand::Clear, 0);
		render_interface->StencilCommand(StencilCommand::Write, mask_padding);
		geometry_padding.Render(element_offset_in_texture);
		render_interface->StencilCommand(StencilCommand::Write, mask_border);
		geometry_border.Render(element_offset_in_texture);
		render_interface->StencilCommand(StencilCommand::WriteDisable);

		geometry.Render(element_offset_in_texture);

		for (int shadow_index = (int)shadow_list.size() - 1; shadow_index >= 0; shadow_index--)
		{
			const Shadow& shadow = shadow_list[shadow_index];

			const bool inset = shadow.inset;
			const Colourb shadow_colors[4] = {shadow.color, shadow.color, shadow.color, shadow.color};

			Vector4f spread_radii = radii;
			for (int i = 0; i < 4; i++)
			{
				float& radius = spread_radii[i];
				float spread_factor = (inset ? -1.f : 1.f);
				if (radius < shadow.spread_distance)
				{
					const float ratio_minus_one = (radius / shadow.spread_distance) - 1.f;
					spread_factor *= 1.f + ratio_minus_one * ratio_minus_one * ratio_minus_one;
				}
				radius = Math::Max(radius + spread_factor * shadow.spread_distance, 0.f);
			}

			Geometry shadow_geometry;

			// Generate the shadow box
			for (int i = 0; i < element->GetNumBoxes(); i++)
			{
				Vector2f offset;
				Box box = element->GetBox(i, offset);
				const float signed_spread_distance = (inset ? -shadow.spread_distance : shadow.spread_distance);
				offset -= Vector2f(signed_spread_distance);

				for (int j = 0; j < (int)Box::NUM_EDGES; j++)
				{
					Box::Edge edge = (Box::Edge)j;
					const float new_size = box.GetEdge(Box::PADDING, edge) + signed_spread_distance;
					box.SetEdge(Box::PADDING, edge, new_size);
				}

				GeometryUtilities::GenerateBackgroundBorder(&shadow_geometry, box, offset, spread_radii, shadow.color,
					inset ? nullptr : shadow_colors);
			}

			const bool has_blur = (shadow.blur_radius > 0.5f) || true;
			CompiledEffectHandle fullscreen_color = render_interface->CompileEffect("color", Dictionary{{"color", Variant(shadow.color)}});
			CompiledEffectHandle blur = {};
			if (has_blur)
				blur = render_interface->CompileEffect("blur", Dictionary{{"radius", Variant(shadow.blur_radius)}});


			render_interface->ExecuteRenderCommand(RenderCommand::StackPush);

			render_interface->EnableScissorRegion(false);

			if (inset)
			{
				render_interface->StencilCommand(StencilCommand::Write, mask_inset, mask_inset);
				shadow_geometry.Render(shadow.offset + element_offset_in_texture);
				render_interface->StencilCommand(StencilCommand::WriteDisable);

				render_interface->StencilCommand(StencilCommand::TestEqual, 0, mask_inset);
				render_interface->RenderEffect(fullscreen_color, RenderSource::Stack, RenderTarget::Stack);

				render_interface->StencilCommand(StencilCommand::Clear, 0, mask_inset);

				if (has_blur)
				{
					render_interface->EnableScissorRegion(false); // TODO: Scissoring
					render_interface->StencilCommand(StencilCommand::TestEqual, mask_padding, mask_padding);
					render_interface->RenderEffect(blur, RenderSource::Stack, RenderTarget::StackBelow);
					//render_interface->EnableScissorRegion(true);
				}
			}
			else
			{
				shadow_geometry.Render(shadow.offset + element_offset_in_texture);

				if (has_blur)
				{
					render_interface->StencilCommand(StencilCommand::TestEqual, 0);
					render_interface->RenderEffect(blur, RenderSource::Stack, RenderTarget::StackBelow);
				}
			}

			render_interface->ExecuteRenderCommand(RenderCommand::StackPop);
			render_interface->StencilCommand(StencilCommand::TestDisable, 0);

			if (has_blur)
				render_interface->ReleaseCompiledEffect(blur);
			render_interface->ReleaseCompiledEffect(fullscreen_color);
		}

		render_interface->EnableScissorRegion(false);

		shadow_texture = render_interface->ExecuteRenderCommand(RenderCommand::StackToTexture, Vector2f(), texture_dimensions);

		render_interface->ExecuteRenderCommand(RenderCommand::StackPop);

		Colourb color_white = Colourb(255, 255, 255, 255);
		Vertex vertices[4];
		int indices[6];
		GeometryUtilities::GenerateQuad(vertices, indices, -element_offset_in_texture, texture_dimensions, color_white);
		shadow_geometry = render_interface->CompileGeometry(vertices, 4, indices, 6, shadow_texture);

		ElementUtilities::ApplyTransform(element, render_interface);
		ElementUtilities::SetClippingRegion(element);
	}
}

} // namespace Rml
