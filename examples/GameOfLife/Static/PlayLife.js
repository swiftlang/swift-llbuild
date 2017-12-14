// This source file is part of the Swift.org open source project
//
// Copyright 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

/// Get an individual board frame.
function getFrame(frameNumber, completion) {
    var request = new XMLHttpRequest();
    request.open('GET', '/frame/' + frameNumber, true);
    request.onload = function() {
        if (request.status != 200) {
            alert("unexpected error during frame request")
        }
        completion(JSON.parse(request.responseText))
    };
    request.onerror = function() {
        alert("unexpected error during frame request")
    };
    request.send();
}

/// Play an animation of the game of life.
function init() {
    // The current frame.
    var currentFrame = 0
    var timeout = 100
    
    function onTimer() {
        getFrame(currentFrame, function(frame) {
            // Get an empty SVG to draw in.
            var currentSVG = document.getElementById("lifeBoard");
            var parent = currentSVG.parentElement
            var svg = currentSVG.cloneNode(false);
            parent.removeChild(currentSVG);
            parent.appendChild(svg);

            for (y = 0; y < frame.height; ++y) {
                for (x = 0; x < frame.width; ++x) {
                    var c = document.createElementNS("http://www.w3.org/2000/svg", "rect");
                    c.setAttributeNS(null, "x", 5 + x * 10);
                    c.setAttributeNS(null, "y", 5 + (frame.height - y - 1) * 10);
                    c.setAttributeNS(null, "width", 10);
                    c.setAttributeNS(null, "height", 10);
                    c.setAttributeNS(null, "fill", frame.data[y][x] ? "black" : "white");
                    c.setAttributeNS(null, "stroke", "none");
                    svg.appendChild(c);
                }
            }
            currentFrame = (currentFrame + 1) % 32
            setTimeout(onTimer, timeout)
        });
    }
    setTimeout(onTimer, timeout)    
}
