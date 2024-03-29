#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup(){
	gui.setup(nullptr, false);
	seam_editor.Setup();
}

//--------------------------------------------------------------
void ofApp::update(){
	seam_editor.Update();
}

//--------------------------------------------------------------
void ofApp::draw(){
	
	seam_editor.Draw();

	if (show_gui) {
		gui.begin();

		seam_editor.GuiDraw();

		gui.end();
		gui.draw();
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
