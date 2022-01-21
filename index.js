'use strict';

module.exports= () => {
	if (process.platform === 'win32') {
		return require('./lib/windows.js')(options);
	}

	throw new Error('Windows only');
}

module.exports.sync = () => {
	if (process.platform === 'win32') {
		return require('./lib/windows.js').sync(options);
	}

	throw new Error('Windows only');
}
