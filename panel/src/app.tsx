import React from 'react';
import 'ray';

interface Props {
  children: React.ReactNode;
}

export default class App extends React.Component<Props> {
  render() {
    return this.props.children;
  }
}
