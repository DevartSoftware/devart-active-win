'use strict';

module.exports= options => {
	if (process.platform === 'win32') {
		return require('./lib/windows.js')(options);
	}

	throw new Error('Windows only');
}

module.exports.sync = options => {
	if (process.platform === 'win32') {
		return require('./lib/windows.js').sync(options);
	}

	throw new Error('Windows only');
}
