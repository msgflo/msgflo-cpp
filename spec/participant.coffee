msgflo = require 'msgflo'
path = require 'path'
chai = require 'chai' unless chai
heterogenous = require '../node_modules/msgflo/spec/heterogenous.coffee'

participants =
  'C++Repeat': [path.join __dirname, '..', 'build', './repeat-cpp']

describe 'Participants', ->
  address = 'amqp://localhost'
  g =
    broker: null
    commands: participants

  beforeEach (done) ->
    g.broker = msgflo.transport.getBroker address
    g.broker.connect done
  afterEach (done) ->
    g.broker.disconnect done

  names = Object.keys g.commands
  names.forEach (name) ->
    heterogenous.testParticipant g, name



