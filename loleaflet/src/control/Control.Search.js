/*
 * L.Control.Search is used for searching text in the document
 */

L.Control.Search = L.Control.extend({
	options: {
		position: 'topleft',
		searchTitle: 'Search document',
		prevText: '&#x25B2',
		prevTitle: 'Previous',
		nextText: '&#x25BC',
		nextTitle: 'Next',
		cancelText: '&#x2716',
		cancelTitle: 'Cancel'
	},

	onAdd: function (map) {
		var searchName = 'leaflet-control-search',
		    container = L.DomUtil.create('div', searchName + ' leaflet-bar'),
		    options = this.options;

		this._searchBar = this._createSearchBar(options.searchTitle,
				searchName + '-bar', container, this._searchStart);
		this._prevButton  = this._createButton(options.prevText, options.prevTitle,
				searchName + '-prev', container, this._searchPrev);
		this._nextButton = this._createButton(options.nextText, options.nextTitle,
				searchName + '-next', container, this._searchNext);
		this._cancelButton = this._createButton(options.cancelText, options.cancelTitle,
				searchName + '-cancel', container, this._cancel);
		L.DomUtil.setStyle(this._cancelButton, 'display', 'none');

		this._updateDisabled();

		return container;
	},

	onRemove: function (map) {
	},

	disable: function () {
		this._disabled = true;
		this._updateDisabled();
		return this;
	},

	enable: function () {
		this._disabled = false;
		this._updateDisabled();
		return this;
	},

	_searchStart: function (e) {
		if (!this._disabled && e.keyCode === 13 && this._searchBar.value !== '' ) {
			L.DomUtil.setStyle(this._cancelButton, 'display', 'inline-block');
			// TODO update protocol
			//this._map.socket.send('search ' + this._searchBar.value);
		}
	},

	_searchPrev: function (e) {
		if (!this._disabled) {
			this._map.fire('searchprev');
		}
	},

	_searchNext: function (e) {
		if (!this._disabled) {
			this._map.fire('searchnext');
		}
	},

	_cancel: function (e) {
		if (!this._disabled) {
			L.DomUtil.setStyle(this._cancelButton, 'display', 'none');
			this._map.fire('clearselection');
			this._map.fire('cancelsearch');
		}
	},

	_createSearchBar: function(title, className, container, fn) {
		var bar = L.DomUtil.create('input', className, container);
		bar.type = 'text';
		bar.title = title;

		L.DomEvent
			.on(bar, 'keyup', L.DomEvent.stop)
			.on(bar, 'keyup', fn, this);

		return bar;
	},

	_createButton: function (text, title, className, container, fn) {
		// TODO create it as it is done for zoom, css knowledge required
		var button = L.DomUtil.create('button', className, container);
		button.innerHTML = text;
		button.title = title;

		L.DomEvent
		    .on(button, 'mousedown dblclick', L.DomEvent.stopPropagation)
		    .on(button, 'click', L.DomEvent.stop)
		    .on(button, 'click', fn, this)
		    .on(button, 'click', this._refocusOnMap, this);

		return button;
	},

	_updateDisabled: function () {
		var map = this._map,
			className = 'leaflet-disabled';

		L.DomUtil.removeClass(this._prevButton, className);
		L.DomUtil.removeClass(this._nextButton, className);
        // TODO disable next/prev buttons depending on curent focused result
	}
});

L.Map.mergeOptions({
	searchControl: true
});

L.Map.addInitHook(function () {
    this.searchControl = new L.Control.Search();
    this.addControl(this.searchControl);
});

L.control.search = function (options) {
	return new L.Control.Search(options);
};