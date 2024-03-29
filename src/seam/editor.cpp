#include "editor.h"
#include "imgui/src/imgui.h"
#include "imgui/src/imgui_node_editor.h"
#include "imgui/src/blueprints/builders.h"

#include "hash.h"
#include "imgui-utils/properties.h"

using namespace seam;
namespace im = ImGui;
namespace ed = ax::NodeEditor;

namespace {
	const char* POPUP_NAME_NEW_NODE = "Create New Node";
	const char* WINDOW_NAME_NODE_MENU = "Node Properties Menu";
}

void Editor::Setup() {
	node_editor_context = ed::CreateEditor();
}

void Editor::Draw() {

	std::sort(nodes_to_draw.begin(), nodes_to_draw.end(), &IEventNode::CompareDrawOrder);
	for (auto n : nodes_to_draw) {
		n->Draw();
	}

	if (selected_node && selected_node->IsVisual()) {
		selected_node->DrawToScreen();
	}

	// TODO draw a final image to the screen
	// probably want multiple viewports / docking?
}

void Editor::UpdateVisibleNodeGraph(IEventNode* n) {
	// traverse parents and update them before traversing this node
	// parents are sorted by update order, so that any "shared parents" are updated first
	int16_t last_update_order = -1;
	for (auto p : n->parents) {
		assert(last_update_order <= p.node->update_order);
		last_update_order = p.node->update_order;
		UpdateVisibleNodeGraph(p.node);
	}

	// now, this node can update, if it's dirty
	if (n->dirty) {
		n->Update(ofGetElapsedTimef());
		n->dirty = false;

		// if this is a visual node, it will need to be re-drawn now
		if (n->IsVisual()) {
			nodes_to_draw.push_back(n);
		}
	}
}

void Editor::Update() {
	// traverse the parent tree of each visible visual node and determine what needs to update
	nodes_to_draw.clear();

	// before traversing the graphs of visible nodes, dirty nodes which update every frame
	for (auto n : nodes_update_over_time) {
		// set the dirty flag directly so children aren't affected;
		// just because the node updates over time doesn't mean it will be dirtied every frame,
		// for instance in the case of a timer which fires every XX seconds
		n->dirty = true;
	}

	for (auto n : visible_nodes) {
		UpdateVisibleNodeGraph(n);
	}
}

void Editor::GuiDrawPopups() {
	const ImVec2 open_popup_position = ImGui::GetMousePos();

	ed::Suspend();

	ed::NodeId node_id;
	if (ed::ShowBackgroundContextMenu()) {
		show_create_dialog = true;
		ImGui::OpenPopup(POPUP_NAME_NEW_NODE);
	} else if (ed::ShowNodeContextMenu(&node_id)) {
		// TODO this is a right click on the node, not left click
	}
	// TODO there are more contextual menus, see blueprints-example.cpp line 1545

	if (ImGui::BeginPopup(POPUP_NAME_NEW_NODE)) {
		// TODO specify an input or output type here if new_link_pin != nullptr
		NodeId new_node_id = factory.DrawCreatePopup();
		if (new_node_id != 0) {
			show_create_dialog = false;
			IEventNode* node = CreateAndAdd(new_node_id);
			ed::SetNodePosition((ed::NodeId)node, open_popup_position);


			// TODO handle new_link_pin
			new_link_pin = nullptr;
		}

		ImGui::EndPopup();
	} else {
		show_create_dialog = false;
	}

	ed::Resume();
}

void Editor::GuiDraw() {

	im::Begin("Seam Editor", nullptr);

	ed::SetCurrentEditor(node_editor_context);

	// remember the editor cursor's start position and the editor's window position,
	// so we can offset other draws on top of the node editor's window
	const ImVec2 editor_cursor_start_pos = ImGui::GetCursorPos();

	ed::Begin("Event Flow");

	ed::Utilities::BlueprintNodeBuilder builder;
	for (auto n : nodes) {
		n->GuiDraw(builder);
	}

	for (const auto& l : links) {
		// TODO this runs a lot, might be better to store Pins in the Links list directly,
		// instead of PinInput* and PinOutput* which require the pointer follow to get the data we want
		ed::Link(
			(ed::LinkId)&l,
			(ed::PinId)l.first->pin,
			(ed::PinId)&l.second->pin
		);
	}

	// query if node(s) have been selected with left click
	// the last selected node should be shown in the properties editor
	std::vector<ed::NodeId> selected_nodes;
	selected_nodes.resize(ed::GetSelectedObjectCount());
	int nodes_count = ed::GetSelectedNodes(selected_nodes.data(), static_cast<int>(selected_nodes.size()));
	if (nodes_count) {
		// the last selected node is the one we'll show in the properties editor
		selected_node = selected_nodes.back().AsPointer<IEventNode>();
	} else {
		selected_node = nullptr;
	}

	// if the create dialog isn't up, handle node graph interactions
	if (!show_create_dialog) {
		// are we trying to create a new pin or node connection? if so, visualize it
		if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f)) {
			auto showLabel = [](const char* label, ImColor color) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
				auto size = ImGui::CalcTextSize(label);

				auto padding = ImGui::GetStyle().FramePadding;
				auto spacing = ImGui::GetStyle().ItemSpacing;

				ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

				auto rectMin = ImGui::GetCursorScreenPos() - padding;
				auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

				auto drawList = ImGui::GetWindowDrawList();
				drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
				ImGui::TextUnformatted(label);
			};

			// visualize potential new links
			ed::PinId start_pin_id = 0, end_pin_id = 0;
			if (ed::QueryNewLink(&start_pin_id, &end_pin_id)) {
				// the pin ID is the pointer to the Pin itself
				// figure out which Pin is the input pin, and which is the output pin
				Pin* pin_in = start_pin_id.AsPointer<Pin>();
				Pin* pin_out = end_pin_id.AsPointer<Pin>();
				if ((pin_in->flags & PinFlags::INPUT) != PinFlags::INPUT) {
					// in and out are reversed, swap them
					std::swap(pin_in, pin_out);
				}
				assert(pin_in && pin_out);

				if ((void*)pin_in == (void*)pin_out) {
					ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
				} else if (pin_in->type != pin_out->type) {
					showLabel("x Pins must be of the same type", ImColor(45, 32, 32, 180));
					ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
				} else if (
					(pin_in->flags & PinFlags::INPUT) != PinFlags::INPUT
					|| (pin_out->flags & PinFlags::OUTPUT) != PinFlags::OUTPUT
				) {
					showLabel("x Connections must be made from input to output", ImColor(45, 32, 32, 180));
					ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);

				} else {
					showLabel("+ Create Link", ImColor(32, 45, 32, 180));
					if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {

						bool connected = Connect(pin_out, pin_in);
						assert(connected);
					}
				}
			}

			// visualize potential new node 
			ed::PinId pin_id = 0;
			if (ed::QueryNewNode(&pin_id)) {
				// figure out if it's an input or output pin
				PinInput* pin_in = dynamic_cast<PinInput*>(pin_id.AsPointer<PinInput>());
				if (pin_in != nullptr) {
					new_link_pin = pin_in->pin;
				} else {
					PinOutput* pin_out = dynamic_cast<PinOutput*>(pin_id.AsPointer<PinOutput>());
					assert(pin_out != nullptr);
					new_link_pin = &pin_out->pin;
				}
				
				if (new_link_pin) {
					showLabel("+ Create Node", ImColor(32, 45, 32, 180));
				}

				if (ed::AcceptNewItem()) {
					show_create_dialog = true;
					// I don't understand why the example does this
					// newNodeLinkPin = FindPin(pin_id);
					// newLinkPin = nullptr;
					ed::Suspend();
					ImGui::OpenPopup(POPUP_NAME_NEW_NODE);
					ed::Resume();
				}
			}
		} else {
			new_link_pin = nullptr;
		}

		ed::EndCreate();

		// visualize deletion interactions, if any
		if (ed::BeginDelete()) {
			ed::LinkId link_id = 0;
			while (ed::QueryDeletedLink(&link_id)) {
				if (ed::AcceptDeletedItem()) {
					Link* link = dynamic_cast<Link*>(link_id.AsPointer<Link>());
					assert(link);
					auto it = std::find_if(links.begin(), links.end(), [link](const Link& other) {
						return link->first == other.first && link->second == other.second;
					});
					assert(it != links.end());
					bool disconnected = Disconnect(&it->second->pin, it->first->pin);
					assert(disconnected);
				}
			}

			ed::NodeId node_id = 0;
			while (ed::QueryDeletedNode(&node_id)) {
				if (ed::AcceptDeletedItem()) {

					// TODO TODO TODO

					/*auto id = std::find_if(s_Nodes.begin(), s_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
					if (id != s_Nodes.end())
						s_Nodes.erase(id);*/
				}
			}
		}
		ed::EndDelete();
	}

	GuiDrawPopups();

	ed::End();

	if (selected_node) {
		
		ImVec2 window_size = im::GetContentRegionAvail();
		ImVec2 window_pos = im::GetWindowPos();
		ImVec2 child_size = ImVec2(256, 256);
		im::SetNextWindowPos(ImVec2(
			window_pos.x + window_size.x,
			editor_cursor_start_pos.y + window_pos.y)
			// add padding
			+ ImVec2(-8.f, 8.f),
			0,
			ImVec2(1.f, 0.f)
		);

		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

		if (im::BeginChild(WINDOW_NAME_NODE_MENU, child_size, true)) {
			ImGui::Text("Update: %d, Draw: %d", selected_node->update_order, selected_node->draw_order);
			ImGui::Text("Pins:");
			bool dirty = props::DrawPinInputs(selected_node);
			ImGui::Text("Properties:");
			dirty = selected_node->GuiDrawPropertiesList() || dirty;
			if (dirty) {
				selected_node->SetDirty();
			}
		}
		im::EndChild();
		
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(1);

	}


	im::End();
}

IEventNode* Editor::CreateAndAdd(NodeId node_id) {
	IEventNode* node = factory.Create(node_id);
	if (node != nullptr) {
		// handle book keeping for the new node

		// TODO do nodes really need to know their IDs?
		node->id = node_id;

		nodes.push_back(node);

		if (node->IsVisual()) {
			// probably temporary: add to the list of visible nodes up front
			auto it = std::upper_bound(visible_nodes.begin(), visible_nodes.end(), node, &IEventNode::CompareDrawOrder);
			visible_nodes.insert(it, node);
		}

		if (node->UpdatesOverTime()) {
			nodes_update_over_time.push_back(node);
		}

		// add input and output pins to the pins_to_nodes list
		// leaving this easy for now -- just add each pin and then sort the whole list
		// nodes probably won't be frequently created, so this doesn't need to be super fast
		size_t size;
		PinInput* inputs = node->PinInputs(size);
		for (size_t i = 0; i < size; i++) {
			PinToNode ptn;
			ptn.pin = inputs[i].pin;
			ptn.node = node;
			pins_to_nodes.push_back(ptn);
		}

		PinOutput* outputs = node->PinOutputs(size);
		for (size_t i = 0; i < size; i++) {
			PinToNode ptn;
			ptn.pin = &outputs[i].pin;
			ptn.node = node;
			pins_to_nodes.push_back(ptn);
		}

		std::sort(pins_to_nodes.begin(), pins_to_nodes.end());
	}

	return node;
}

IEventNode* Editor::CreateAndAdd(const std::string_view node_name) {
	return CreateAndAdd(SCHash(node_name.data(), node_name.length()));
}

bool Editor::Connect(IEventNode* parent, Pin* pin_co, IEventNode* child, Pin* pin_ci) {
	// pin_co == pin connection out
	// pin_ci == pin connection in
	// names are hard :(
	assert((pin_ci->flags & PinFlags::INPUT) == PinFlags::INPUT);
	assert((pin_co->flags & PinFlags::OUTPUT) == PinFlags::OUTPUT);
	assert(pin_co->type == pin_ci->type);

	// find the structs that contain the pins to be connected
	// also more validations: make sure pin_out is actually an output pin of node_out, and same for the input
	PinInput* pin_in = FindPinInput(child, pin_ci);
	PinOutput* pin_out = FindPinOutput(parent, pin_co);
	assert(pin_in && pin_out);

	if (pin_in == nullptr || pin_out == nullptr) {
		return false;
	}

	// create the connection
	pin_out->connections.push_back(*pin_in);

	// add to the links list
	links.push_back(Link(pin_in, pin_out));

	// add to each node's parents and children list
	// if this connection rearranged the node graph, 
	// its traversal order will need to be recalculated
	const bool is_new_child = parent->AddChild(child);
	const bool is_new_parent = child->AddParent(parent);
	const bool rearranged = is_new_parent || is_new_child;

	if (rearranged) {
		RecalculateTraversalOrder(child, true, true);
	}

	return true;
}

bool Editor::Connect(Pin* pin_out, Pin* pin_in) {
	// find the node each pin is connected to
	IEventNode* node_in = MapPinToNode(pin_in);
	IEventNode* node_out = MapPinToNode(pin_out);
	assert(node_in && node_out);

	return Connect(node_out, pin_out, node_in, pin_in);
}

bool Editor::Disconnect(IEventNode* parent, Pin* pin_co, IEventNode* child, Pin* pin_ci) {
	assert((pin_ci->flags & PinFlags::INPUT) == PinFlags::INPUT);
	assert((pin_co->flags & PinFlags::OUTPUT) == PinFlags::OUTPUT);
	assert(pin_co->type == pin_ci->type);

	PinInput* pin_in = FindPinInput(child, pin_ci);
	PinOutput* pin_out = FindPinOutput(parent, pin_co);
	assert(pin_in && pin_out);

	if (pin_in == nullptr || pin_out == nullptr) {
		return false;
	}

	// remove from pin_out's connections list
	{
		auto it = std::find(pin_out->connections.begin(), pin_out->connections.end(), *pin_in);
		assert(it != pin_out->connections.end());
		pin_out->connections.erase(it);
	}

	// remove from links list
	{
		Link l(pin_in, pin_out);
		auto it = std::find(links.begin(), links.end(), l);
		assert(it != links.end());
		links.erase(it);
	}

	// track if we need to update traversal order
	bool rearranged = false;

	// remove or decrement from receivers / transmitters lists
	{
		auto it = std::find(parent->children.begin(), parent->children.end(), child);
		assert(it != parent->children.end());
		if (it->conn_count == 1) {
			parent->children.erase(it);
			rearranged = true;
		} else {
			it->conn_count -= 1;
		}
	}

	{
		auto it = std::find(child->parents.begin(), child->parents.end(), parent);
		assert(it != child->parents.end());
		if (it->conn_count == 1) {
			child->parents.erase(it);
			rearranged = true;
		} else {
			it->conn_count -= 1;
		}
	}

	if (rearranged) {
		// the child node and its children need to recalculate draw and update order now
		RecalculateTraversalOrder(child, true, true);
	}

	return true;
}

bool Editor::Disconnect(Pin* pin_out, Pin* pin_in) {
	IEventNode* node_out = MapPinToNode(pin_out);
	IEventNode* node_in = MapPinToNode(pin_in);
	assert(node_out && node_in);
	
	return Disconnect(node_out, pin_out, node_in, pin_in);
}

int16_t Editor::RecalculateUpdateOrder(IEventNode* node) {
	// update order is always max of parents' update order + 1
	if (node->update_order != -1) {
		return node->update_order;
	}

	// TODO! deal with feedback pins

	int16_t max_parents_update_order = -1;
	for (auto parent : node->parents) {
		max_parents_update_order = std::max(
			max_parents_update_order,
			// TODO checking the parent update order may not be necessary,
			// if the children list is sorted for its recursive traversal
			RecalculateUpdateOrder(parent.node)
		);
	}
	node->update_order = max_parents_update_order + 1;

	// recursively update children
	for (auto child : node->children) {
		RecalculateUpdateOrder(child.node);
	}

	return node->update_order;
}

int16_t Editor::RecalculateDrawOrder(IEventNode* node) {
	// the draw order of a node is the max of its parents' draw order,
	// plus 1 IF this node is a visual node
	if (node->draw_order != -1) {
		return node->draw_order;
	}

	// TODO handle feedback pins (they should not be traversed for this purpose)

	int16_t max_parents_draw_order = 0;
	for (auto parent : node->parents) {
		max_parents_draw_order = std::max(
			max_parents_draw_order, 
			RecalculateDrawOrder(parent.node)
		);
	}
	bool is_visual = (node->flags & NodeFlags::IS_VISUAL);
	node->draw_order = max_parents_draw_order + is_visual;
	
	// recursively update children
	for (auto child : node->children) {
		RecalculateDrawOrder(child.node);
	}

	return node->draw_order;
}

void Editor::InvalidateChildren(IEventNode* node, bool recalc_update, bool recalc_draw) {
	{
		// if this node has already been invalidated, don't go over it again
		bool needs_update_invalidated = recalc_update && node->update_order != -1;
		bool needs_draw_invalidated = recalc_draw && node->draw_order != -1;
		if (!needs_update_invalidated && !needs_draw_invalidated) {
			return;
		}
	}

	node->update_order = recalc_update ? -1 : node->update_order;
	node->draw_order = recalc_draw ? -1 : node->draw_order;

	for (auto child : node->children) {
		InvalidateChildren(child.node, recalc_update, recalc_draw);
	}
}

void Editor::RecalculateTraversalOrder(IEventNode* node, bool recalc_update, bool recalc_draw) {
	// invalidate this node and its children
	InvalidateChildren(node, recalc_update, recalc_draw);

	if (recalc_update) {
		RecalculateUpdateOrder(node);
	}

	if (recalc_draw) {
		RecalculateDrawOrder(node);
	}
}

IEventNode* Editor::MapPinToNode(Pin* pin) {
	auto it = std::lower_bound(pins_to_nodes.begin(), pins_to_nodes.end(), pin);
	return it != pins_to_nodes.end() && it->pin == pin ? it->node : nullptr;
}

PinInput* Editor::FindPinInput(IEventNode* node, Pin* pin) {
	PinInput* pin_in = nullptr;
	size_t size;
	PinInput* pin_inputs = node->PinInputs(size);
	for (size_t i = 0; i < size && pin_in == nullptr; i++) {
		if (pin_inputs[i].pin == pin) {
			pin_in = &pin_inputs[i];
		}
	}
	return pin_in;
}

PinOutput* Editor::FindPinOutput(IEventNode* node, Pin* pin) {
	PinOutput* pin_out = nullptr;
	size_t size;
	PinOutput* pin_outputs = node->PinOutputs(size);
	for (size_t i = 0; i < size && pin_out == nullptr; i++) {
		if (&pin_outputs[i].pin == pin) {
			pin_out = &pin_outputs[i];
		}
	}
	return pin_out;
}

