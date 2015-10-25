/*
 * L.Map.Mouse is handling mouse interaction with the document
 */

L.Map.mergeOptions({
	mouse: true
});

L.Map.Mouse = L.Handler.extend({

	initialize: function (map) {
		this._map = map;
		this._mouseEventsQueue = [];
	},

	addHooks: function () {
		this._map.on('mousedown mouseup mouseover mouseout mousemove dblclick',
			this._onMouseEvent, this);
	},

	removeHooks: function () {
		this._map.off('mousedown mouseup mouseover mouseout mousemove dblclick',
			this._onMouseEvent, this);
	},

	LOButtons: {
		left: 1,
		middle: 2,
		right: 4
	},

	JSButtons: {
		left: 0,
		middle: 1,
		right: 2
	},

	_onMouseEvent: function (e) {
		var docLayer = this._map._docLayer;
		if (!docLayer || (this._map.slideShow && this._map.slideShow.fullscreen)) {
			return;
		}
		if (docLayer._graphicMarker && docLayer._graphicMarker.isDragged) {
			return;
		}

		for (var key in docLayer._selectionHandles) {
			if (docLayer._selectionHandles[key].isDragged) {
				return;
			}
		}

		var modifier = this._map.keyboard.modifier;
		var buttons = 0;
		buttons |= e.originalEvent.button === this.JSButtons.left ? this.LOButtons.left : 0;
		buttons |= e.originalEvent.button === this.JSButtons.middle ? this.LOButtons.middle : 0;
		buttons |= e.originalEvent.button === this.JSButtons.right ? this.LOButtons.right : 0;

		if (e.type === 'mousedown') {
			docLayer._resetPreFetching();
			this._mouseDown = true;
			if (this._holdMouseEvent) {
				clearTimeout(this._holdMouseEvent);
			}
			var mousePos = docLayer._latLngToTwips(e.latlng);
			this._mouseEventsQueue.push(L.bind(function() {
				this._postMouseEvent('buttondown', mousePos.x, mousePos.y, 1, buttons, modifier);
			}, docLayer));
			this._holdMouseEvent = setTimeout(L.bind(this._executeMouseEvents, this), 500);
		}
		else if (e.type === 'mouseup') {
			this._mouseDown = false;
			if (this._map.dragging.enabled()) {
				if (this._mouseEventsQueue.length === 0) {
					// mouse up after panning
					return;
				}
			}
			clearTimeout(this._holdMouseEvent);
			this._holdMouseEvent = null;
			if (this._clickTime && Date.now() - this._clickTime <= 250) {
				// double click, a click was sent already
				this._mouseEventsQueue = [];
				return;
			}
			else {
				this._clickTime = Date.now();
				mousePos = docLayer._latLngToTwips(e.latlng);
				var timeOut = 250;
				if (docLayer._permission === 'edit') {
					timeOut = 0;
				}
				this._mouseEventsQueue.push(L.bind(function() {
					var docLayer = this._map._docLayer;
					// if it's a click or mouseup after selecting
					if (this._mouseEventsQueue.length > 1) {
						// it's a click
						if (docLayer._permission === 'view') {
							docLayer._map.setPermission('edit');
						}
					}
					this._mouseEventsQueue = [];
					docLayer._postMouseEvent('buttonup', mousePos.x, mousePos.y, 1, buttons, modifier);
					docLayer._textArea.focus();
				}, this));
				this._holdMouseEvent = setTimeout(L.bind(this._executeMouseEvents, this), timeOut);

				for (key in docLayer._selectionHandles) {
					var handle = docLayer._selectionHandles[key];
					if (handle._icon) {
						L.DomUtil.removeClass(handle._icon, 'leaflet-not-clickable');
					}
				}
			}
		}
		else if (e.type === 'mousemove' && this._mouseDown) {
			if (this._holdMouseEvent) {
				clearTimeout(this._holdMouseEvent);
				this._holdMouseEvent = null;
				if (this._map.dragging.enabled()) {
					// The user just panned the document
					this._mouseEventsQueue = [];
					return;
				}
				for (var i = 0; i < this._mouseEventsQueue.length; i++) {
					// synchronously execute old mouse events so we know that
					// they arrive to the server before the move command
					this._mouseEventsQueue[i]();
				}
				this._mouseEventsQueue = [];
			}
			if (!this._map.dragging.enabled()) {
				mousePos = docLayer._latLngToTwips(e.latlng);
				docLayer._postMouseEvent('move', mousePos.x, mousePos.y, 1, buttons, modifier);
				for (key in docLayer._selectionHandles) {
					handle = docLayer._selectionHandles[key];
					if (handle._icon) {
						L.DomUtil.addClass(handle._icon, 'leaflet-not-clickable');
					}
				}
			}
		}
		else if (e.type === 'dblclick') {
			mousePos = docLayer._latLngToTwips(e.latlng);
			docLayer._postMouseEvent('buttondown', mousePos.x, mousePos.y, 1, buttons, modifier);
			docLayer._postMouseEvent('buttondown', mousePos.x, mousePos.y, 2, buttons, modifier);
			docLayer._postMouseEvent('buttonup', mousePos.x, mousePos.y, 2, 1, buttons, modifier);
			docLayer._postMouseEvent('buttonup', mousePos.x, mousePos.y, 1, 1, buttons, modifier);
		}
	},

	_executeMouseEvents: function () {
		this._holdMouseEvent = null;
		for (var i = 0; i < this._mouseEventsQueue.length; i++) {
			this._mouseEventsQueue[i]();
		}
		this._mouseEventsQueue = [];
	}
});

L.Map.addInitHook('addHandler', 'mouse', L.Map.Mouse);
