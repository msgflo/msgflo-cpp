msgflo = require 'msgflo'
path = require 'path'
chai = require 'chai' unless chai
heterogenous = require '../node_modules/msgflo/spec/heterogenous.coffee'

participants =
  'C++Repeat': [path.join __dirname, '..', 'build', './examples/repeat']

# Note: most require running an external broker service
transports =
  #'MQTT': 'mqtt://localhost'
  'AMQP': 'amqp://localhost'

transportTests = (g, address) ->

  beforeEach (done) ->
    g.broker = msgflo.transport.getBroker address
    g.broker.connect done
  afterEach (done) ->
    g.broker.disconnect done

  names = Object.keys g.commands
  names.forEach (name) ->
    heterogenous.testParticipant g, name


describe 'Participants', ->
  g =
    broker: null
    commands: participants

  Object.keys(transports).forEach (type) =>
    describe "#{type}", () ->
      address = transports[type]
      transportTests g, address
