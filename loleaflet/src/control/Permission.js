/*
 * Document permission handler
 */
L.Map.include({
	setPermission: function (perm) {
		this._docLayer._permission = perm;
		if (perm === 'edit') {
			L.Socket.sendMessage('requestloksession');
			this.dragging.disable();
		}
		else if (perm === 'view' || perm === 'readonly') {
			this.dragging.enable();
			// disable all user interaction, will need to add keyboard too
			this._docLayer._onUpdateCursor();
			this._docLayer._clearSelections();
			this._docLayer._onUpdateTextSelection();
		}
		this.fire('updatepermission', {perm : perm});
	},

	enableSelection: function () {
		if (this._docLayer._permission === 'edit') {
			return;
		}
		L.Socket.sendMessage('requestloksession');
		this.dragging.disable();
	},

	disableSelection: function () {
		if (this._docLayer._permission === 'edit') {
			return;
		}
		this.dragging.enable();
	},

	isSelectionEnabled: function () {
		return !this.dragging.enabled();
	},

	getPermission: function () {
		return this._docLayer._permission;
	}
});
