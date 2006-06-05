// **********************************************************************
//
// Copyright (c) 2003-2006 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Glacier2/ProxyVerifier.h>

#include <vector>
#include <string>

using namespace std;
using namespace Ice;

//
// TODO: Some of the address matching helper classes can probably be
// refactored out. It's a question of whether to keep really simple
// differences refactored into separate classes or go to fewer but
// slightly smarter classes.
//

//
// Proxy rule implementations.
//
namespace Glacier2
{

//
// A numeric range that can be encoded in a filter.
//
struct Range
{
    long start;
    long end;
};

static void 
parseGroup(const string& parameter, vector<int>& validPorts, vector<Range>& ranges)
{
    istringstream istr(parameter);
    while(!istr.eof())
    {
	ws(istr);
	int value;
	if(!(istr >> value))
	{
	    InitializationException ex(__FILE__, __LINE__);
	    ex.reason = "expected number";
	    throw ex;
	}
	ws(istr);
	if(!istr.eof())
	{
	    char c;
	    if(istr >> c)
	    {
		if(c == ',')
		{
		    validPorts.push_back(value);
		}
		else if(c == '-')
		{
		    Range r;
		    r.start = value;
		    ws(istr);
		    if(istr.eof())
		    {
			InitializationException ex(__FILE__, __LINE__);
			ex.reason = "Unterminated range";
			throw ex;
		    }
		    if(!(istr >> value))
		    {
			InitializationException ex(__FILE__, __LINE__);
			ex.reason = "expected number";
			throw ex;
		    }
		    r.end = value;
		    ws(istr);
		    if(!istr.eof())
		    {
			istr >> c;
			if(c != ',')
			{
			    InitializationException ex(__FILE__, __LINE__);
			    ex.reason = "expected comma separator";
			    throw ex;
			}
		    }
		    ranges.push_back(r);
		}
		else if(!istr.eof())
		{
		    InitializationException ex(__FILE__, __LINE__);
		    ex.reason = "unexpected trailing character";
		    throw ex;
		}
	    }
	}
	else
	{
	    validPorts.push_back(value);
	}
    }
}

//
// Base class for address matching operations.
//
class AddressMatcher
{
public:
    virtual ~AddressMatcher() {} 
    virtual bool match(const string&, string::size_type& pos) = 0;

    virtual const char* toString() const = 0;
};

//
// Match the start of a string (i.e. position == 0). Occurs when filter
// string starts with a set of characters followed by a wildcard or
// numeric range. 
//
class StartsWithString : public AddressMatcher
{
public:
    StartsWithString(const string& criteria):
	_criteria(criteria),
	_description("starts with " + criteria)
    {
    }

    bool 
    match(const string& space, string::size_type& pos)
    {
	assert(pos == 0);
	bool result = strncmp(space.c_str(), _criteria.c_str(), _criteria.size()) == 0;
	if(result)
	{
	    pos += _criteria.size();
	}
	return result;
    }

    virtual const char*
    toString() const
    {
	return _description.c_str();
    }

private:
    string _criteria;
    string _description;
};

//
// Match the end portion of a string. Occurs when a filter string starts
// with a wildcard or numeric range, but ends with a string.
//
class EndsWithString : public AddressMatcher
{
public:
    EndsWithString(const string& criteria):
	_criteria(criteria),
	_description("ends with " + criteria)
    {
    }

    bool 
    match(const string& space, string::size_type& pos)
    {
	if(space.size() - pos < _criteria.size())
	{
	    return false;
	}

	string::size_type spaceEnd = space.size();
	for(string::size_type i = _criteria.size(); i > 0; --i)
	{
	    if(space[spaceEnd - 1] != _criteria[i-1])
	    {
		return false;
	    }
	    --spaceEnd;
	}
	return true;
    }

    virtual const char*
    toString() const
    {
	return _description.c_str();
    }

private:
    string _criteria;
    string _description;
};

class MatchesString : public AddressMatcher
{
public: 
    MatchesString(const string& criteria):
	_criteria(criteria),
	_description("matches " + criteria)
    {
    }

    bool
    match(const string& space, string::size_type& pos)
    {
	if(strncmp(space.c_str(), _criteria.c_str(), _criteria.size()) == 0)
	{
	    pos += _criteria.size();
	    return true;
	}
	return false;
    }

    virtual const char*
    toString() const
    {
	return _description.c_str();
    }

private:
    string _criteria;
    string _description;
};

//
// Match against somewhere within a string. Occurs when a filter
// contains a string bounded by wildcards, or numeric ranges. e.g. *bar*.com.
//
class ContainsString : public AddressMatcher
{
public:
    ContainsString(const string& criteria):
	_criteria(criteria),
	_description("contains " + criteria)
    {
    }

    bool
    match(const string& space, string::size_type& pos)
    {
	string::size_type offset = space.find(_criteria, pos);
	if(offset == string::npos)
	{
	    return false;
	}
	pos = offset + _criteria.size() +1;
	return true;
    }

    virtual const char*
    toString() const
    {
	return _description.c_str();
    }

private:
    string _criteria;
    string _description;
};

//
// Match a number against a range of values. This occurs when the filter
// contains a numeric range or group of numeric values. e.g. foo[1-3,
// 10].bar.com. Also used to match port numbers and ranges.
//
class MatchesNumber : public AddressMatcher
{
public:
    MatchesNumber(const vector<int>& values, const vector<Range>& ranges, 
		  const char* descriptionPrefix = "matches "):
	_values(values),
	_ranges(ranges)
    {
	ostringstream ostr;
	ostr << descriptionPrefix;
	{
	    bool start = true;
	    for(vector<int>::const_iterator i = values.begin(); i != values.end(); ++i)
	    {
		if(start)
		{
		    if(values.size() > 1)
		    {
			ostr << "one of ";
		    }
		    start = false;
		}
		else
		{
		    ostr << ", ";
		}

		ostr << *i;
	    }
	}
	if(values.size() > 0 && ranges.size() > 0)
	{
	    ostr << " or ";
	}
	{
	    bool start = true;
	    for(vector<Range>::const_iterator i = ranges.begin(); i != ranges.end(); ++i)
	    {
		if(start)
		{
		    start = false;
		}
		else
		{
		    ostr << ", or";
		}
		ostr << i->start << " up to " << i->end;
	    }
	}
	ostr << ends;
	_description = ostr.str();
    }

    bool
    match(const string & space, string::size_type& pos)
    {
	istringstream istr(space.substr(pos));
	int val;
	if(!(istr >> val))
	{
	    return false;
	}
	pos += istr.tellg();
	{
	    for(vector<int>::const_iterator i = _values.begin(); i != _values.end(); ++i)
	    {
		if(val == *i)
		{
		    return true;
		}
	    }
	}
	{
	    for(vector<Range>::const_iterator i = _ranges.begin(); i != _ranges.end(); ++i)
	    {
		if((val >= i->start) && (val <= i->end))
		{
		    return true;
		}
	    }
	}
	return false;
    }

    virtual const char*
    toString() const
    {
	return _description.c_str();
    }

private:
    const vector<int> _values;
    const vector<Range> _ranges;
    string _description;
};

//
// Occurs when a numeric range is preceded by a wildcard.
//
class ContainsNumberMatch : public MatchesNumber
{
public:
    ContainsNumberMatch(const vector<int>& values, const vector<Range>& ranges):
	MatchesNumber(values, ranges, "contains ")
    {
    }

    bool 
    match(const string& space, string::size_type& pos)
    {
	while(true) 
	{
	    pos = space.find_first_of("0123456789", pos);
	    if(pos == string::npos)
	    {
		return false;
	    }

	    if(MatchesNumber::match(space, pos))
	    {
		return true;
	    }
	}
	return false;
    }
};

class EndsWithNumber : public MatchesNumber
{
public:
    EndsWithNumber(const vector<int>& values, const vector<Range>& ranges):
	MatchesNumber(values, ranges, "ends with ")
    {
    }

    bool 
    match(const string& space, string::size_type& pos)
    {
	pos = space.find_last_not_of("0123456789", pos);
	if(pos == space.size()-1)
	{
	    return false;
	}

	return MatchesNumber::match(space, pos);
    }
};

//
// AddressMatcher factories abstract away the logic of which matching
// objects need to be created depending on the state of the filter
// string parsing. Similar to changing a tool that produces the right
// result depending on how far along you are in the job, the factories
// are selected according to what transition has occurred while parsing
// the filter string.
//
class AddressMatcherFactory
{
public:
    virtual ~AddressMatcherFactory() {} 

    virtual AddressMatcher* 
    create(const string& criteria) = 0;

    virtual AddressMatcher* 
    create(const vector<int>& ports, const vector<Range>& ranges) = 0;
};

class StartFactory : public AddressMatcherFactory
{
public:
    AddressMatcher* 
    create(const string& criteria)
    {
	return new StartsWithString(criteria);
    }

    AddressMatcher* 
    create(const vector<int>& ports, const vector<Range>& ranges)
    {
	return new MatchesNumber(ports, ranges);
    }
};

class WildCardFactory : public AddressMatcherFactory
{
public:
    AddressMatcher* 
    create(const string& criteria)
    {
	return new ContainsString(criteria);
    }

    AddressMatcher* 
    create(const vector<int>& ports, const vector<Range>& ranges)
    {
	return new ContainsNumberMatch(ports, ranges);
    }
};

class FollowingFactory : public AddressMatcherFactory
{
public:
    AddressMatcher* 
    create(const string& criteria)
    {
	return new MatchesString(criteria);
    }

    AddressMatcher* 
    create(const vector<int>& ports, const vector<Range>& ranges)
    {
	return new MatchesNumber(ports, ranges);
    }
};

class EndsWithFactory : public AddressMatcherFactory
{
public:
    AddressMatcher*
    create(const string& criteria)
    {
	return new EndsWithString(criteria);
    }

    AddressMatcher* 
    create(const vector<int>& ports, const vector<Range>& ranges)
    {
	return new EndsWithNumber(ports, ranges);
    }
};

//
// A proxy validation rule encapsulating an address filter.
//
class AddressRule : public Glacier2::ProxyRule
{
public:
    AddressRule(const vector<AddressMatcher*>& address, MatchesNumber* port) :
	_addressRules(address),
	_portMatcher(port)
    {
    }

    ~AddressRule()
    {
	for(vector<AddressMatcher*>::const_iterator i = _addressRules.begin(); i != _addressRules.end(); ++i)
	{
	    delete *i;
	}
	delete _portMatcher;
    }

    virtual bool 
    check(const ObjectPrx& prx) const
    {
	EndpointSeq endpoints = prx->ice_getEndpoints();
	for(EndpointSeq::const_iterator i = endpoints.begin(); i != endpoints.end(); ++i)
	{
	    string info = (*i)->toString();
	    string host;
	    if(!extractPart("-h ", info, host))
	    {
		assert(false);
		return false;
	    }
	    string port;
	    if(!extractPart("-p ", info, port))
	    {
		assert(false);
		return false;
	    }
	    string::size_type pos = 0;
	    if(!_portMatcher || _portMatcher->match(port, pos))
	    {
		pos = 0;
		for(vector<AddressMatcher*>::const_iterator i = _addressRules.begin(); i != _addressRules.end(); ++i)
		{
		    if(!(*i)->match(host, pos))
		    {
			cerr << "XXX- failed to match on " << (*i)->toString() << endl;
			dump();
			return false;
		    }
		}
	    }
	}
	return true;
    }

    void 
    dump() const
    {
	cerr << "address(";
	for(vector<AddressMatcher*>::const_iterator i = _addressRules.begin(); i != _addressRules.end(); ++i)
	{
	    cerr << (*i)->toString() << " ";
	}
	if(_portMatcher != 0)
	{
	    cerr << "):port(" << _portMatcher->toString() << " ";
	}
	cerr << ")" << endl;
    }

private:

    bool 
    extractPart(const char* opt, const string& source, string& result) const
    {
	string::size_type start = source.find(opt);
	if(start == string::npos)
	{
	    return false;
	}
	start += strlen(opt);
	string::size_type end = source.find(' ', start);
	if(end != string::npos)
	{
	    result = source.substr(start, end - start);
	}
	else
	{
	    result = source.substr(start);
	}
	return true;
    }

    vector<AddressMatcher*> _addressRules;
    MatchesNumber* _portMatcher;
};

static void
parseProperty(const string& property, vector<ProxyRule*>& rules)
{
    StartFactory startsWithFactory;
    WildCardFactory wildCardFactory;
    EndsWithFactory endsWithFactory;
    FollowingFactory followingFactory;
    vector<ProxyRule*> allRules;
    try
    {
	istringstream propertyInput(property);

	while(!propertyInput.eof() && propertyInput.good())
	{
	    MatchesNumber* portMatch = 0;
	    vector<AddressMatcher*> currentRuleSet;

	    string parameter;
	    ws(propertyInput);
	    propertyInput >> parameter;

	    string portInfo;
	    string::size_type portPortion = parameter.find(':');

	    string addr;
	    if(portPortion != string::npos)
	    {
		addr = parameter.substr(0, portPortion);
		string port = parameter.substr(portPortion + 1);
		string::size_type openBracket = port.find('[');
		if(openBracket != string::npos)
		{
		    ++openBracket;
		    string::size_type closeBracket = port.find(']', openBracket);
		    if(closeBracket == string::npos)
		    {
			InitializationException ex(__FILE__, __LINE__);
			ex.reason = "unclosed group";
			throw ex;
		    }
		    port = port.substr(openBracket, closeBracket-openBracket);
		}
		vector<int> ports;
		vector<Range> ranges;
		parseGroup(port, ports, ranges);
		portMatch = new MatchesNumber(ports, ranges);
	    }
	    else
	    {
		addr = parameter;
	    }

	    //
	    // The addr portion can contain alphanumerics, * and
	    // ranges.
	    //
	    string::size_type current = 0;

	    if(current == addr.size())
	    {
		InitializationException ex(__FILE__, __LINE__);
		ex.reason = "expected address information before ':'";
		throw ex;
	    }

	    //
	    // TODO: assuming that there is no leading or trailing whitespace. This
	    // should probably be confirmed.
	    //
	    assert(!isspace(parameter[current]));
	    assert(!isspace(addr[addr.size() -1]));

	    if(current != 0)
	    {
		addr = addr.substr(current);
	    }

	    string::size_type mark = 0;
	    bool inGroup = false;
	    AddressMatcherFactory* currentFactory = &startsWithFactory;

	    for(current = 0; current < addr.size(); ++current)
	    {
		if(addr[current] == '*')
		{
		    if(inGroup)
		    {
			InitializationException ex(__FILE__, __LINE__);
			ex.reason = "wildcards not permitted in groups";
			throw ex;
		    }
		    //
		    // current == mark when the wildcard is at the head of a
		    // string or directly after a group.
		    //
		    if(current != mark)
		    {
			currentRuleSet.push_back(currentFactory->create(addr.substr(mark, current-mark)));
		    }
		    currentFactory = &wildCardFactory;
		    mark = current + 1;
		}
		else if(addr[current] == '[')
		{
		    // ??? what does it mean if current == mark?
		    if(current != mark)
		    {
			currentRuleSet.push_back(currentFactory->create(addr.substr(mark, current-mark)));
			currentFactory = &followingFactory;
		    }
		    inGroup = true;
		    mark = current + 1;
		}
		else if(addr[current] == ']')
		{
		    if(!inGroup)
		    {
			InitializationException ex(__FILE__, __LINE__);
			ex.reason = "group close without group start";
			throw ex;
		    }
		    inGroup = false;
		    if(mark == current)
		    {
			InitializationException ex(__FILE__, __LINE__);
			ex.reason = "empty group";
			throw ex;
		    }
		    string group = addr.substr(mark, current - mark);
		    vector<int> numbers;
		    vector<Range> ranges;
		    parseGroup(group, numbers, ranges);
		    currentRuleSet.push_back(currentFactory->create(numbers, ranges));
		    currentFactory = &followingFactory;
		    mark = current + 1;
		}
	    }
	    currentFactory = &endsWithFactory;

	    if(inGroup)
	    {
		InitializationException ex(__FILE__, __LINE__);
		ex.reason = "unclosed group";
		throw ex;
	    }
	    if(mark != current)
	    {
		currentRuleSet.push_back(currentFactory->create(addr.substr(mark, current - mark)));
	    }
	    allRules.push_back(new AddressRule(currentRuleSet, portMatch));
	}
    }
    catch(...)
    {
	for(vector<ProxyRule*>::const_iterator i = allRules.begin(); i != allRules.end(); ++i)
	{
	    delete *i;
	}
	throw;
    }
    rules = allRules;
}

//
// Helper function for checking a rule set. 
//
static bool
match(const vector<ProxyRule*>& rules, const ObjectPrx& proxy)
{
    for(vector<ProxyRule*>::const_iterator i = rules.begin(); i != rules.end(); ++i)
    {
	if((*i)->check(proxy))
	{
	    return true;
	}
    }
    return false;
}

//
// MaxEndpointsRule returns 'true' if the proxy exceeds the configured
// number of endpoints.
//
class MaxEndpointsRule : public ProxyRule
{
public:
    MaxEndpointsRule(const CommunicatorPtr communicator, const string& count, int traceLevel) :
	_communicator(communicator),
	_traceLevel(traceLevel)
    {
	istringstream s(count);
	if(!(s >> _count) || !s.eof())
	{
	    InitializationException ex(__FILE__, __LINE__);
	    ex.reason = "Error parsing MaxEndpoints property";
	    throw ex;
	}
	if(_count <= 0)
	{
	    InitializationException ex(__FILE__, __LINE__);
	    ex.reason = "MaxEndpoints must be greater than 1";
	    throw ex;
	}
    }

    bool
    check(const ObjectPrx& p) const
    {
	EndpointSeq endpoints = p->ice_getEndpoints();
	bool result = (endpoints.size() > _count);
	if(_traceLevel >= 3)
	{
	    Trace out(_communicator->getLogger(), "Glacier2");
	    out << _communicator->proxyToString(p) << (result ? " exceeds " : " meets ") 
		<< "endpoint count restriction\n";
	}
	return result;
    }

private:
    const CommunicatorPtr _communicator;
    const int _traceLevel;
    unsigned long _count;
};

} // End proxy rule implementations.

Glacier2::ProxyVerifier::ProxyVerifier(const CommunicatorPtr& communicator, const char* ruleSet):
    _communicator(communicator),
    _traceLevel(communicator->getProperties()->getPropertyAsInt("Glacier2.Trace.Reject"))
{
    //
    // Evaluation order is dependant on how the rules are stored to the
    // rules vectors. 
    //
    string s = communicator->getProperties()->getProperty("Glacier2.Filter.Address.Accept");
    if(s != "")
    {
	Glacier2::parseProperty(s, _acceptRules);
    }

    s = communicator->getProperties()->getProperty("Glacier2.Filter.Address.Reject");
    if(s != "")
    {
	Glacier2::parseProperty(s, _rejectRules);
    }

    s = communicator->getProperties()->getProperty("Glacier2.Filter.MaxProxyLength");
    if(s != "")
    {
	_rejectRules.push_back(new MaxEndpointsRule(communicator, s, _traceLevel));
    }
    _acceptOverrides =
	communicator->getProperties()->getPropertyAsIntWithDefault("Glacier2.Filter.Address.AcceptOverride", 0) != 0;
}

Glacier2::ProxyVerifier::~ProxyVerifier()
{
    for(vector<ProxyRule*>::const_iterator i = _acceptRules.begin(); i != _acceptRules.end(); ++i)
    {
	delete (*i);
    }
    for(vector<ProxyRule*>::const_iterator j = _rejectRules.begin(); j != _rejectRules.end(); ++j)
    {
	delete (*j);
    }
}

bool
Glacier2::ProxyVerifier::verify(const ObjectPrx& proxy)
{
    //
    // No rules have been defined so we accept all.
    //
    if(_acceptRules.size() == 0 && _rejectRules.size() == 0)
    {
	return true;
    }

    bool result;

    if(_rejectRules.size() == 0)
    {
	//
	// If there are no reject rules, we assume "reject all".
	//
	result = match(_acceptRules, proxy);
    }
    else if(_acceptRules.size() == 0)
    {
	//
	// If no accept rules are defined we assume accept all.
	//
	result = !match(_rejectRules, proxy);
    }
    else
    {
	//
	// _acceptOverrides indicates that any accept rules can be
	// overriden by a reject rule. This allows the user to refine
	// the allow filter's without having to specify exclusions in
	// the accept filter's regular expression. Conversely if
	// _acceptOverrides is not set then accept rules are allowed to
	// override any reject rules that match.
	//
	// Note that there is implicit additional meaning in the
	// _acceptOverrides. If true, then the overall evaluation
	// context is 'default reject'. Otherwise there would be no
	// point in considering the allow filters and we might as well
	// just check the reject filters. Conversely, if false then
	// overall context is 'default accept'. Otherwise the accept
	// filters would be meaningless, only the reject filters would
	// matter.
	//
	if(_acceptOverrides)
	{
	    //
	    // In this context we are default accept, there is not point
	    // of running the accept filters if there is no rejection
	    // match.
	    //
	    result = !match(_rejectRules, proxy);
	    if(!result)
	    {
		result = match(_acceptRules, proxy);
	    }
	}
	else
	{
	    //
	    // In this context we are default reject, there is no point
	    // of running the reject filters if there is no accept
	    // match.
	    //
	    result = match(_acceptRules, proxy);
	    if(result)
	    {
		result = !match(_rejectRules, proxy);
	    }
	}
    }

    //
    // The proxy rules take care of the tracing for higher trace levels.
    //
    if(_traceLevel > 0)
    {
	Trace out(_communicator->getLogger(), "Glacier2");
	if(result)
	{
	    out << "accepted proxy " << _communicator->proxyToString(proxy) << '\n';
	}
	else
	{
	    out << "rejected proxy " << _communicator->proxyToString(proxy) << '\n';
	}
    }
    return result;
}
