//(function(){
//'use strict';

function Transaction() {
  this.initialize();
}

Transaction.prototype = {

    ID:		0,
    funcs:	[],

    initialize: function() {
	this.funcs = [];
	if(typeof(CTransaction) === 'undefined'){
	  log.warn('CTransactions not available.');
	  this.ID = 15;
	  this.ct = null;
	} else {
	  this.ct = new CTransaction();
	  this.ID = this.ct.newTransaction();
	  //log.trace('Transaction '+this.ID+' has '+this.funcs.length+' elements');
	}
    },

    toString:   function() {
        return '{ size : '+this.funcs.length+', id : '+this.ID+' }';
    },

    push: function(f){
	//log.trace('Adding function '+f);
	this.funcs.push(f);
    },

    abort: function(){
	delete taFunctions;
    },

    commit: function(){
    	log.debug('Commiting transaction '+this.ID+' with '+this.funcs.length+' elements');
	if(this.ct){
	    this.ct.lock(this.ID);
	}
    	for(var i = 0; i < this.funcs.length; i++){
    	    this.funcs[i]();
    	}
	if(this.ct){
	    this.ct.commit(this.ID);
	}
    },
}; 